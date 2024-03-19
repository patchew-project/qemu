/*
 * QEMU Guest Agent win32-specific command implementations for SSH keys.
 * The implementation is opinionated and expects the SSH implementation to
 * be OpenSSH.
 *
 * Copyright IBM Corp. 2024
 *
 * Authors:
 *  Aidan Leuck <aidan_leuck@selinc.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <qga-qapi-types.h>
#include <stdbool.h>
#include "qapi/error.h"
#include "qga-qapi-commands.h"
#include "lmaccess.h"
#include "guest-agent-core.h"
#include "lmerr.h"
#include "lmapibuf.h"
#include "shlobj.h"
#include "limits.h"
#include "userenv.h"
#include "commands-ssh-core.h"
#include "sddl.h"
#include <aclapi.h>

#define MAX_PATH_CHAR MAX_PATH * sizeof(char)
#define AUTHORIZED_KEY_FILE "authorized_keys"
#define AUTHORIZED_KEY_FILE_ADMIN "administrators_authorized_keys"
#define LOCAL_SYSTEM_SID "S-1-5-18"
#define ADMIN_SID "S-1-5-32-544"
#define USER_COUNT 3

// Converts from a standard string to a Windows wide string.
// it is a 16-bit wide character used to store Unicode encoded as UTF-16LE/
// some Windows API functions require this format of the string as opposed to just
// the normal c char*. This function attempts to convert a standard string to
// a wide string if it is possible. Some multibyte characters are not supported
// so it could throw an error.
// Read more here:
// https://learn.microsoft.com/en-us/cpp/cpp/char-wchar-t-char16-t-char32-t?view=msvc-170
// parameters:
// string - String to convert to a wchar.
// errp - Error pointer that will set errors if they are converted
// returns - The converted string or NULL if an error occurs.
static wchar_t *string_to_wide(const char *string, Error **errp)
{
    // Get the length of the string + 1 for the NULL terminating character.
    size_t requiredSize = strlen(string) + 1;

    // Create memory for the wide string.
    wchar_t *wideString = g_malloc((requiredSize) * sizeof(wchar_t));
    if (!wideString)
    {
        error_setg(errp, "Failed to allocate memory for wide string.");
        return NULL;
    }

    // Convert to wide string
    size_t size = mbstowcs(wideString, string, requiredSize);
    if (size == (size_t)-1)
    {
        error_setg(errp, "Couldn't convert string to wide string. Invalid multibyte character encountered");

        // Free the allocated memory if we failed to convert the string.
        if (wideString)
        {
            g_free(wideString);
        }

        return NULL;
    }

    // Return the pointer back to the user.
    return g_steal_pointer(&wideString);
}

// Frees userInfo structure. This implements the g_auto cleanup
// for the structure.
void free_userInfo(PWindowsUserInfo info)
{
    if (info->sshDirectory)
    {
        g_free(info->sshDirectory);
    }
    if (info->authorizedKeyFile)
    {
        g_free(info->authorizedKeyFile);
    }
    if (info->SSID)
    {
        LocalFree(info->SSID);
    }
    if (info->username)
    {
        g_free(info->username);
    }

    g_free(info);
}

// Gets the admin SSH folder for OpenSSH. OpenSSH does not store
// the authorized_key file in the users home directory for security reasons and instead
// stores it at %PROGRAMDATA%/ssh. This function returns the path to that directory
// on the users machine
// parameters: errp -> error structure to set when an error occurs
// returns: The path to the ssh folder in %PROGRAMDATA% or NULl if an error occurred.
static char *get_admin_ssh_folder(Error **errp)
{
    // Allocate memory for the program data path
    g_autofree char *programDataPath = g_malloc(MAX_PATH_CHAR);
    char *authkeys_path = NULL;
    PWSTR pgDataW;

    // Get the KnownFolderPath on the machine.
    HRESULT folderResult = SHGetKnownFolderPath(&FOLDERID_ProgramData, 0, NULL, &pgDataW);
    if (folderResult != S_OK)
    {
        error_setg(errp, "Failed to retrieve ProgramData folder");
        return NULL;
    }

    // Convert from a wide string back to a standard character string.
    size_t writtenBytes = wcstombs(programDataPath, pgDataW, MAX_PATH_CHAR);

    // Free the Windows allocated string.
    CoTaskMemFree(pgDataW);
    if (writtenBytes == (size_t)-1)
    {
        error_setg(errp, "Failed to convert program data path to char string");
        return NULL;
    }

    // Build the path to the file.
    authkeys_path = g_build_filename(programDataPath, "ssh", NULL);
    return authkeys_path;
}

//  Gets the path to the SSH folder for the specified user. If the user is an admin it returns
//  the ssh folder located at %PROGRAMDATA%/ssh. If the user is not an admin it returns %USERPROFILE%/.ssh
//  parameters:
//  username -> Username to get the SSH folder for
//  isAdmin -> Whether the user is an admin or not
//  errp -> Error structure to set any errors that occur.
//  returns: path to the ssh folder as a string.

static char *get_ssh_folder(const char *username, const bool isAdmin, Error **errp)
{
    if (isAdmin)
    {
        return get_admin_ssh_folder(errp);
    }

    // If not an Admin the SSH key is in the user directory.
    DWORD maxSize = MAX_PATH_CHAR;
    g_autofree LPSTR profilesDir = g_malloc(maxSize);

    // Get the user profile directory on the machine.
    BOOL ret = GetProfilesDirectory(profilesDir, &maxSize);
    if (!ret)
    {
        error_setg_win32(errp, GetLastError(), "failed to retrieve profiles directory");
        return NULL;
    }

    // Builds the filename
    return g_build_filename(profilesDir, username, ".ssh", NULL);
}

// Sets the access control on the authorized_keys file and any ssh folders that need
// to be created. For administrators the required permissions on the file/folders are
// that only administrators and the LocalSystem account can access the folders.
// For normal user accounts only the specified user, LocalSystem and Administrators can
// have access to the key.
// parameters:
// userInfo -> pointer to structure that contains information about the user
// PACL -> pointer to an access control structure that will be set upon successful completion of the function.
// errp -> error structure that will be set upon error.
// returns: 1 upon success 0 upon failure.
static bool create_acl(PWindowsUserInfo userInfo, PACL *pACL, Error **errp)
{
    g_autofree PSECURITY_DESCRIPTOR pSD = NULL;
    g_autofree PEXPLICIT_ACCESS pExplicitAccess = NULL;
    PSID systemPSID = NULL;
    PSID adminPSID = NULL;
    PSID userPSID = NULL;

    // If the user is an admin only admins and LocalSystem have access so two ACL's
    // if the user is not an admin, the user, admin and LocalSystem will have access so three ACL's.
    int numUsers = userInfo->isAdmin ? 2 : 3;

    // Allocate memory
    pExplicitAccess = g_malloc(sizeof(EXPLICIT_ACCESS) * numUsers);

    // Zero out the allocated memory.
    ZeroMemory(pExplicitAccess, numUsers * sizeof(EXPLICIT_ACCESS));

    // If the user is not an admin add the user creating the key or folder to the list.
    if (!userInfo->isAdmin)
    {
        // Get a pointer to the internal SID object in Windows
        bool converted = ConvertStringSidToSid(userInfo->SSID, &userPSID);
        if (!converted)
        {
            error_setg_win32(errp, GetLastError(), "failed to retrieve user %s SID", userInfo->username);
            goto error;
        }

        // Set the permissions for the user.
        pExplicitAccess[2].grfAccessPermissions = GENERIC_ALL;
        pExplicitAccess[2].grfAccessMode = SET_ACCESS;
        pExplicitAccess[2].grfInheritance = NO_INHERITANCE;
        pExplicitAccess[2].Trustee.TrusteeForm = TRUSTEE_IS_SID;
        pExplicitAccess[2].Trustee.TrusteeType = TRUSTEE_IS_USER;
        pExplicitAccess[2].Trustee.ptstrName = (LPTSTR)userPSID;
    }

    // Create an entry for the system user.
    const char *systemSID = LOCAL_SYSTEM_SID;
    bool converted = ConvertStringSidToSid(systemSID, &systemPSID);
    if (!converted)
    {
        error_setg_win32(errp, GetLastError(), "failed to retrieve system SID");
        goto error;
    }

    // set permissions for system user
    pExplicitAccess[0].grfAccessPermissions = GENERIC_ALL;
    pExplicitAccess[0].grfAccessMode = SET_ACCESS;
    pExplicitAccess[0].grfInheritance = NO_INHERITANCE;
    pExplicitAccess[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    pExplicitAccess[0].Trustee.TrusteeType = TRUSTEE_IS_USER;
    pExplicitAccess[0].Trustee.ptstrName = (LPTSTR)systemPSID;

    // Create an entry for the admin user.
    const char *adminSID = ADMIN_SID;
    converted = ConvertStringSidToSid(adminSID, &adminPSID);
    if (!converted)
    {
        error_setg_win32(errp, GetLastError(), "failed to retrieve Admin SID");
        goto error;
    }

    pExplicitAccess[1].grfAccessPermissions = GENERIC_ALL;
    pExplicitAccess[1].grfAccessMode = SET_ACCESS;
    pExplicitAccess[1].grfInheritance = NO_INHERITANCE;
    pExplicitAccess[1].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    pExplicitAccess[1].Trustee.TrusteeType = TRUSTEE_IS_GROUP;
    pExplicitAccess[1].Trustee.ptstrName = (LPTSTR)adminPSID;

    // Put the entries in an ACL object.
    PACL pNewACL = NULL;
    DWORD setResult = SetEntriesInAcl(numUsers, pExplicitAccess, NULL, &pNewACL);

    // Set the user provided pointer to the allocated pointer
    *pACL = pNewACL;

    if (setResult != ERROR_SUCCESS)
    {
        error_setg_win32(errp, GetLastError(), "failed to set ACL entries for user %s %lu", userInfo->username, setResult);
        goto error;
    }

    // free memory
    LocalFree(systemPSID);
    LocalFree(adminPSID);

    if (userPSID)
    {
        LocalFree(userPSID);
    }

    return true;

error:
    // On error free memory and return false.
    if (systemPSID)
    {
        LocalFree(systemPSID);
    }
    if (adminPSID)
    {
        LocalFree(adminPSID);
    }
    if (userPSID)
    {
        LocalFree(userPSID);
    }

    return false;
}

// Create the SSH directory for the user and d sets appropriate permissions.
// In general the directory will be %PROGRAMDATA%/ssh if the user is an admin.
// %USERPOFILE%/.ssh if not an admin
// parameters:
// userInfo -> Contains information about the user
// errp -> Structure that will contain errors if the function fails.
// returns: zero upon failure, 1 upon success
static bool create_ssh_directory(WindowsUserInfo *userInfo, Error **errp)
{

    PACL pNewACL = NULL;
    g_autofree PSECURITY_DESCRIPTOR pSD = NULL;

    // Gets the approparite ACL for the user
    if (!create_acl(userInfo, &pNewACL, errp))
    {
        goto error;
    }

    // Allocate memory for a security descriptor
    pSD = g_malloc(SECURITY_DESCRIPTOR_MIN_LENGTH);
    if (!InitializeSecurityDescriptor(pSD, SECURITY_DESCRIPTOR_REVISION))
    {
        error_setg_win32(errp, GetLastError(), "Failed to initialize security descriptor");
        goto error;
    }

    // Associate the security descriptor with the ACL permissions.
    if (!SetSecurityDescriptorDacl(pSD, TRUE, pNewACL, FALSE))
    {
        error_setg_win32(errp, GetLastError(), "Failed to set security descriptor ACL");
        goto error;
    }

    // Set the security attributes on the folder
    SECURITY_ATTRIBUTES sAttr;
    sAttr.bInheritHandle = FALSE;
    sAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    sAttr.lpSecurityDescriptor = pSD;

    // Create the directory with the created permissions
    BOOL created = CreateDirectory(userInfo->sshDirectory, &sAttr);
    if (!created)
    {
        error_setg_win32(errp, GetLastError(), "failed to create directory %s", userInfo->sshDirectory);
        goto error;
    }

    // Free memory
    LocalFree(pNewACL);
    return true;
error:
    if (pNewACL)
    {
        LocalFree(pNewACL);
    }

    return false;
}

// Sets permissions on the authorized_key_file that is created.
// parameters: userInfo -> Information about the user
// errp -> error structure that will contain errors upon failure
// returns: 1 upon success, zero upon failure.
static bool set_file_permissions(PWindowsUserInfo userInfo, Error **errp)
{
    PACL pACL = NULL;
    PSID userPSID;

    // Creates the access control structure
    if (!create_acl(userInfo, &pACL, errp))
    {
        goto error;
    }

    // Get the PSID structure for the user based off the string SID.
    bool converted = ConvertStringSidToSid(userInfo->SSID, &userPSID);
    if (!converted)
    {
        error_setg_win32(errp, GetLastError(), "failed to retrieve user %s SID", userInfo->username);
        goto error;
    }

    // Set the ACL on the file.
    if (SetNamedSecurityInfo(userInfo->authorizedKeyFile, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, userPSID, NULL, pACL, NULL) != ERROR_SUCCESS)
    {
        error_setg_win32(errp, GetLastError(), "failed to set file security for file %s", userInfo->authorizedKeyFile);
        goto error;
    }

    LocalFree(pACL);
    LocalFree(userPSID);
    return true;

error:
    if (pACL)
    {
        LocalFree(pACL);
    }
    if (userPSID)
    {
        LocalFree(userPSID);
    }
    return false;
}

// Writes the specified keys to the authenticated keys file.
// parameters:
// userInfo: Information about the user we are writing the authkeys file to.
// authkeys: Array of keys to write to disk
// errp: Error structure that will contain any errors if they occur.
// returns: 1 if succesful, 0 otherwise.
static bool write_authkeys(WindowsUserInfo *userInfo, GStrv authkeys, Error **errp)
{
    g_autofree char *contents = NULL;
    g_autoptr(GError) err = NULL;

    contents = g_strjoinv("\n", authkeys);

    if (!g_file_set_contents(userInfo->authorizedKeyFile, contents, -1, &err))
    {
        error_setg(errp, "failed to write to '%s': %s", userInfo->authorizedKeyFile, err->message);
        return false;
    }

    if (!set_file_permissions(userInfo, errp))
    {
        return false;
    }

    return true;
}

// Retrieves information about a Windows user by their username
// userInfo -> Double pointer to a WindowsUserInfo structure. Upon success, it will be allocated
// with information about the user and need to be freed.
// username -> Name of the user to lookup.
// errp -> Contains any errors that occur.
// returns -> 1 upon success, 0 upon failure.
static bool get_user_info(PWindowsUserInfo *userInfo, const char *username, Error **errp)
{
    DWORD infoLevel = 4;
    LPUSER_INFO_4 uBuf = NULL;
    g_autofree wchar_t *wideUserName = NULL;

    // Converts a string to a Windows wide string since the GetNetUserInfo
    // function requires it.
    wideUserName = string_to_wide(username, errp);
    if (!wideUserName)
    {
        goto error;
    }

    // allocate data
    PWindowsUserInfo uData = g_malloc(sizeof(WindowsUserInfo));

    // Set pointer so it can be cleaned up by the calle, even upon error.
    *userInfo = uData;

    // Find the information
    NET_API_STATUS result = NetUserGetInfo(NULL, wideUserName, infoLevel, (LPBYTE *)&uBuf);
    if (result != NERR_Success)
    {
        // Give a friendlier error message if the user was not found.
        if (result == NERR_UserNotFound)
        {
            error_setg(errp, "User %s was not found", username);
            goto error;
        }

        error_setg(errp, "Received unexpected error when asking for user info: Error Code %lu", result);
        goto error;
    }

    // Get information from the buffer returned by NetUserGetInfo.
    uData->username = g_strdup(username);
    uData->isAdmin = uBuf->usri4_priv == USER_PRIV_ADMIN;
    PSID psid = uBuf->usri4_user_sid;

    char *sidStr = NULL;

    // We store the string representation of the SID not SID structure in memory. Callees wanting
    // to use the SID structure should call ConvertStringSidToSID.
    if (!ConvertSidToStringSid(psid, &sidStr))
    {
        error_setg_win32(errp, GetLastError(), "failed to get SID string for user %s", username);
        goto error;
    }

    // Store the SSID
    uData->SSID = sidStr;

    // Get the SSH folder for the user.
    char *sshFolder = get_ssh_folder(username, uData->isAdmin, errp);
    if (sshFolder == NULL)
    {
        goto error;
    }

    // Get the authorized key file path
    const char *authorizedKeyFile = uData->isAdmin ? AUTHORIZED_KEY_FILE_ADMIN : AUTHORIZED_KEY_FILE;
    char *authorizedKeyPath = g_build_filename(sshFolder, authorizedKeyFile, NULL);

    uData->sshDirectory = sshFolder;
    uData->authorizedKeyFile = authorizedKeyPath;

    // Free
    NetApiBufferFree(uBuf);
    return true;
error:
    if (uBuf)
    {
        NetApiBufferFree(uBuf);
    }

    return false;
}

// Gets the list of authorized keys for a user.
// parameters:
// username -> Username to retrieve the keys for.
// errp -> Error structure that will display any errors through QMP.
// returns: List of keys associated with the user.
GuestAuthorizedKeys *qmp_guest_ssh_get_authorized_keys(const char *username, Error **errp)
{
    GuestAuthorizedKeys *keys = NULL;
    g_auto(GStrv) authKeys = NULL;
    g_autoptr(GuestAuthorizedKeys) ret = NULL;
    g_auto(PWindowsUserInfo) userInfo = NULL;

    // Gets user information
    if (!get_user_info(&userInfo, username, errp))
    {
        return NULL;
    }

    // Reads authekys for the user
    authKeys = read_authkeys(userInfo->authorizedKeyFile, errp);
    if (authKeys == NULL)
    {
        return NULL;
    }

    // Set the GuestAuthorizedKey struct with keys from the file
    ret = g_new0(GuestAuthorizedKeys, 1);
    for (int i = 0; authKeys[i] != NULL; i++)
    {
        g_strstrip(authKeys[i]);
        if (!authKeys[i][0] || authKeys[i][0] == '#')
        {
            continue;
        }

        QAPI_LIST_PREPEND(ret->keys, g_strdup(authKeys[i]));
    }

    // Steal the pointer because it is up for the callee to deallocate the memory.
    keys = g_steal_pointer(&ret);
    return keys;
}

// Adds an ssh key for a user.
// parameters:
// username -> User to add the SSH key to
// strList -> Array of keys to add to the list
// has_reset -> Whether the keys have been reset
// reset -> Boolean to reset the keys (If this is set the existing list will be cleared) and the other key reset.
// errp -> Pointer to an error structure that will get returned over QMP if anything goes wrong.
void qmp_guest_ssh_add_authorized_keys(const char *username, strList *keys,
                                       bool has_reset, bool reset,
                                       Error **errp)
{
    g_auto(PWindowsUserInfo) userInfo = NULL;
    g_auto(GStrv) authkeys = NULL;
    strList *k;
    size_t nkeys, nauthkeys;

    // Make sure the keys given are valid
    if (!check_openssh_pub_keys(keys, &nkeys, errp))
    {
        return;
    }

    // Gets user information
    if (!get_user_info(&userInfo, username, errp))
    {
        return;
    }

    // Determine whether we should reset the keys
    reset = has_reset && reset;
    if (!reset)
    {
        // If we are not resetting the keys, read the existing keys into memory
        authkeys = read_authkeys(userInfo->authorizedKeyFile, NULL);
    }

    // Check that the SSH key directory exists for the user.
    if (!g_file_test(userInfo->sshDirectory, G_FILE_TEST_IS_DIR))
    {
        BOOL success = create_ssh_directory(userInfo, errp);
        if (!success)
        {
            return;
        }
    }

    // Reallocates the buffer to fit the new keys.
    nauthkeys = authkeys ? g_strv_length(authkeys) : 0;
    authkeys = g_realloc_n(authkeys, nauthkeys + nkeys + 1, sizeof(char *));

    // zero out the memory for the reallocated buffer
    memset(authkeys + nauthkeys, 0, (nkeys + 1) * sizeof(char *));

    // Adds the keys
    for (k = keys; k != NULL; k = k->next)
    {
        // Check that the key doesn't already exist
        if (g_strv_contains((const gchar *const *)authkeys, k->value))
        {
            continue;
        }

        authkeys[nauthkeys++] = g_strdup(k->value);
    }

    // Write the authkeys to the file.
    write_authkeys(userInfo, authkeys, errp);
}

// Removes an SSH key for a user
// parameters:
// username -> Username to remove the key from
// strList -> List of strings to remove
// errp -> Contains any errors that occur.
void qmp_guest_ssh_remove_authorized_keys(const char *username, strList *keys,
                                          Error **errp)
{
    g_auto(PWindowsUserInfo) userInfo = NULL;
    g_autofree struct passwd *p = NULL;
    g_autofree GStrv new_keys = NULL; /* do not own the strings */
    g_auto(GStrv) authkeys = NULL;
    GStrv a;
    size_t nkeys = 0;

    // Validates the keys passed in by the user
    if (!check_openssh_pub_keys(keys, NULL, errp))
    {
        return;
    }

    // Gets user information
    if (!get_user_info(&userInfo, username, errp))
    {
        return;
    }

    // Reads the authkeys for the user
    authkeys = read_authkeys(userInfo->authorizedKeyFile, errp);
    if (authkeys == NULL)
    {
        return;
    }

    // Create a new buffer to hold the keys
    new_keys = g_new0(char *, g_strv_length(authkeys) + 1);
    for (a = authkeys; *a != NULL; a++)
    {
        strList *k;

        // Filters out keys that are equal to ones the user specified.
        for (k = keys; k != NULL; k = k->next)
        {
            if (g_str_equal(k->value, *a))
            {
                break;
            }
        }

        if (k != NULL)
        {
            continue;
        }

        new_keys[nkeys++] = *a;
    }

    // Write the new authkeys to the file.
    write_authkeys(userInfo, new_keys, errp);
}
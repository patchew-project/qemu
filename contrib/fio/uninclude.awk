BEGIN {
    # gcc strips #ifdef blocks, which removes the multiple-inclusion guard too.
    # Repair with #pragma once.
    print "#pragma once"
}

{
    # Documentation says builtins are not printed by -dD, but reality disagrees.
    if ($1 == "#" && $3 ~ "\"<built-in>\"") {
        delete_until_hash = 1
        next
    }
    if (delete_until_hash) {
        if ($1 == "#") {
            delete_until_hash = 0
        } else {
           next
       }
   }

    # Handle the delete state: skip files included with <...> and -include,
    # plus their nested includes
    if (delete_depth) {
        if ($1 == "#") {
            if ($4 == "1") {
                delete_depth++
            } else if ($4 == "2") {
                delete_depth--
            }
        }
        if (delete_depth) {
            next
        } else {
            # Out of delete state.  We are on a # directive, if necessary
            # we can use it to set command_line again
            command_line = 0
        }
    }

    # Handle the command-line state: skip -D definitions and -included files
    if ($1 == "#" && $3 == "\"<command-line>\"") {
        command_line = 1
    }

    if (command_line) {
        if ($1 == "#" && $4 == "1") {
            # This is a -included file, go to the delete state
            delete_depth = 1
        }
        next
    }

    if ($1 == "#include") {
        if ($2 ~ /^</) {
            print
            # We printed the #include directive.  Now skip until the # line
            # that enters the included file, and go to the delete state.
            do {
                getline
            } while ($1 == "#" && $4 != "1")
            if ($1 == "#" && $4 == "1") {
                delete_depth = 1
                next
            }
        } else {
            # For local includes we include their content, so the #include
            # directive must go.
            next
        }
    }

    # Remove line directives emitted by the preprocessor
    if ($1 == "#") {
        next
    }

    print
}

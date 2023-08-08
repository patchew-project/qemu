#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

void compare_memory(const void *a, const void *b, size_t n)
{
    const unsigned char *p1 = a;
    const unsigned char *p2 = b;
    for (size_t i = 0; i < n; i++) {
        assert(p1[i] == p2[i]);
    }
}

void test_memcpy()
{
    char src[] = "Hello, world!";
    char dest[20];
    memcpy(dest, src, 13);
    compare_memory(dest, src, 13);
}

void test_strncpy()
{
    char src[] = "Hello, world!";
    char dest[20];
    strncpy(dest, src, 13);
    compare_memory(dest, src, 13);
}

void test_strcpy()
{
    char src[] = "Hello, world!";
    char dest[20];
    strcpy(dest, src);
    compare_memory(dest, src, 13);
}

void test_strcat()
{
    char src[20] = "Hello, ";
    char dst[] = "world!";
    char str[] = "Hello, world!";
    strcat(src, dest);
    compare_memory(src, str, 13);
}

void test_memcmp()
{
    char str1[] = "abc";
    char str2[] = "abc";
    char str3[] = "def";
    assert(memcmp(str1, str2, 3) == 0);
    assert(memcmp(str1, str3, 3) != 0);
}

void test_strncmp()
{
    char str1[] = "abc";
    char str2[] = "abc";
    char str3[] = "def";
    assert(strncmp(str1, str2, 2) == 0);
    assert(strncmp(str1, str3, 2) != 0);
}

void test_strcmp()
{
    char str1[] = "abc";
    char str2[] = "abc";
    char str3[] = "def";
    assert(strcmp(str1, str2) == 0);
    assert(strcmp(str1, str3) != 0);
}

void test_memset()
{
    char buffer[10];
    memset(buffer, 'A', 10);
    int i;
    for (i = 0; i < 10; i++) {
        assert(buffer[i] == 'A');
    }
}

int main()
{
    test_memset();
    test_memcpy();
    test_strncpy();
    test_memcmp();
    test_strncmp();
    test_strcpy();
    test_strcmp();
    test_strcat();

    return EXIT_SUCCESS;
}



void stxp_issue_demo(void *arr)
{
    asm(".align 8\n\t"
        "    mov x0, %[in]\n\t"
        "    mov x18, 0x1000\n\t"
        "    mov x2, 0x0\n\t"
        "    mov x3, 0x0\n\t"
        "loop:\n\t"
        "    prfm  pstl1strm, [x0]\n\t"
        "    ldxp  x16, x17, [x0]\n\t"
        "    stxp  w16, x2, x3, [x0]\n\t"
        "\n\t"
        "    subs x18, x18, 1\n\t"
        "    beq done\n\t"
        "    b loop\n\t"
        "done:\n\t"
        : /* none out */
        : [in] "r" (arr) /* in */
        : "x0", "x2", "x3", "x16", "x17", "x18"); /* clobbers */
}

int main()
{
    char arr[16];
    stxp_issue_demo(&arr);
}

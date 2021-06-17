#include <stdio.h>

int main(int ac, char** av)
{
    int arr[5] = {1, 2, 3, 4, 5};
    int val = getchar() - 48;
    int y = arr[val];

    printf("%d\n", y);

    return 0;
}
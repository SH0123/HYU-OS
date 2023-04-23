#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char* argv[])
{
    if(argc <= 1) {
        exit();
    }
    char* buf = argv[1];
    int ret_val;
    ret_val = myfunction(buf);
    printf(1, "Return value : 0x%x\n", ret_val);
    exit();
}

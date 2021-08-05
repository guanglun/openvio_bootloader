#include "iap.h"

void boot(void)
{
    
}

int iap_jump_to(uint32_t address)
{
    if (((*(uint32_t *)address) & 0x20000000) == 0x20000000)
    {
        system_jump_to(address);
        return 1;
    }
    return 0;
}
extern "C" void boot_sdk_install_drivers_once(void)
{
    /* Disabled in the safe boot command build.
     * Pulling the full SDK HAL/device init into the bootloader can prevent boot
     * command mode from reaching KBOOT:READY on this target.  The SD DMA path is
     * kept as a separate experiment; the default smoke must never brick command
     * access.
     */
}

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <math.h>  
#include "hwlib.h"
#include "socal/socal.h"
#include "socal/hps.h"
#include "socal/alt_gpio.h"
#include "hps_0.h"   // contains PIO_PWM_BASE, PIO_ACCEL_X/Y/Z_BASE :contentReference[oaicite:0]{index=0}

#define HW_REGS_BASE ( ALT_STM_OFST )
#define HW_REGS_SPAN ( 0x04000000 )
#define HW_REGS_MASK ( HW_REGS_SPAN - 1 )

int main(void)
{
    int fd;
    void *virtual_base;

    // PIO virtual addresses
    void *h2p_lw_pwm_addr;
    void *h2p_lw_accel_x_addr;
    void *h2p_lw_accel_y_addr;
    void *h2p_lw_accel_z_addr;

    // open /dev/mem
    if ((fd = open("/dev/mem", (O_RDWR | O_SYNC))) == -1) {
        printf("ERROR: could not open \"/dev/mem\"...\n");
        return 1;
    }

    // map LW bridge region
    virtual_base = mmap(NULL,
                        HW_REGS_SPAN,
                        (PROT_READ | PROT_WRITE),
                        MAP_SHARED,
                        fd,
                        HW_REGS_BASE);

    if (virtual_base == MAP_FAILED) {
        printf("ERROR: mmap() failed...\n");
        close(fd);
        return 1;
    }

    // Map PWM duty-cycle PIO (8-bit)
    h2p_lw_pwm_addr = virtual_base +
        ((unsigned long)(ALT_LWFPGASLVS_OFST + PIO_PWM_BASE) &
         (unsigned long)(HW_REGS_MASK));

    // Map accel X/Y/Z PIOs (16-bit each)
    h2p_lw_accel_x_addr = virtual_base +
        ((unsigned long)(ALT_LWFPGASLVS_OFST + PIO_ACCEL_X_BASE) &
         (unsigned long)(HW_REGS_MASK));

    h2p_lw_accel_y_addr = virtual_base +
        ((unsigned long)(ALT_LWFPGASLVS_OFST + PIO_ACCEL_Y_BASE) &
         (unsigned long)(HW_REGS_MASK));

    h2p_lw_accel_z_addr = virtual_base +
        ((unsigned long)(ALT_LWFPGASLVS_OFST + PIO_ACCEL_Z_BASE) &
         (unsigned long)(HW_REGS_MASK));

    printf("Starting accel->PWM control loop...\n");

    while (1) {
        // Read raw accel values (lower 16 bits of each PIO)
        int16_t ax = (int16_t)(*(volatile uint32_t *)h2p_lw_accel_x_addr & 0xFFFF);
        int16_t ay = (int16_t)(*(volatile uint32_t *)h2p_lw_accel_y_addr & 0xFFFF);
        int16_t az = (int16_t)(*(volatile uint32_t *)h2p_lw_accel_z_addr & 0xFFFF);

        
        static float prev_mag = 0.0f;


        float fx = (float)ax;
        float fy = (float)ay;
        float fz = (float)az;

        float mag = sqrtf(fx*fx + fy*fy + fz*fz);


        float diff = fabsf(mag - prev_mag);


        prev_mag = mag;

        float norm = diff / 2000.0f;

        int duty = (int)(norm * 100.0f);

        if (duty > 100) duty = 100;
        if (duty < 0)   duty = 0;

        *(volatile uint32_t *)h2p_lw_pwm_addr = duty;

        printf("AX=%6d  AY=%6d  AZ=%6d  MAG=%8.1f  DIFF=%6.1f  DUTY=%3d%%\n",
       ax, ay, az, mag, diff, duty);

        // Update ~10 times per second
        usleep(100 * 1000);
    }

    // cleanup (not actually reached in this infinite loop)
    if (munmap(virtual_base, HW_REGS_SPAN) != 0) {
        printf("ERROR: munmap() failed...\n");
        close(fd);
        return 1;
    }

    close(fd);
    return 0;
}

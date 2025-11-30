#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "hwlib.h"
#include "socal/socal.h"
#include "socal/hps.h"
#include "socal/alt_gpio.h"
#include "hps_0.h"

#define HW_REGS_BASE (ALT_STM_OFST)
#define HW_REGS_SPAN (0x04000000)
#define HW_REGS_MASK (HW_REGS_SPAN - 1)

static volatile int16_t g_ax = 0;
static volatile int16_t g_ay = 0;
static volatile int16_t g_az = 0;
static volatile int     g_duty = 0;

void *http_server_thread(void *arg)
{
    int server_fd, client_fd;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    (void)arg;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return NULL;

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(8080);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) return NULL;
    if (listen(server_fd, 5) < 0) return NULL;

    printf("HTTP server listening on port 8080 (http://<board-ip>:8080)\n");

    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&addr, &addrlen);
        if (client_fd < 0) continue;

        int16_t ax = g_ax;
        int16_t ay = g_ay;
        int16_t az = g_az;
        int duty = g_duty;

        char body[512];
        int len_body = snprintf(
            body, sizeof(body),
            "<html><head>"
            "<meta http-equiv=\"refresh\" content=\"1\">"
            "<title>Accel PWM Status</title>"
            "</head>"
            "<body>"
            "<h1>Accel PWM Status</h1>"
            "<p>AX = %d</p>"
            "<p>AY = %d</p>"
            "<p>AZ = %d</p>"
            "<p>PWM Duty = %d%%</p>"
            "</body></html>",
            ax, ay, az, duty
        );

        if (len_body < 0) len_body = 0;
        if (len_body > (int)sizeof(body)) len_body = sizeof(body);

        char header[256];
        int len_header = snprintf(
            header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n",
            len_body
        );

        if (len_header > 0) write(client_fd, header, len_header);
        if (len_body > 0) write(client_fd, body, len_body);

        close(client_fd);
    }

    close(server_fd);
    return NULL;
}

int main(void)
{
    int fd;
    void *virtual_base;

    void *h2p_lw_pwm_addr;
    void *h2p_lw_accel_x_addr;
    void *h2p_lw_accel_y_addr;
    void *h2p_lw_accel_z_addr;

    if ((fd = open("/dev/mem", (O_RDWR | O_SYNC))) == -1) return 1;

    virtual_base = mmap(NULL, HW_REGS_SPAN,
                        (PROT_READ | PROT_WRITE),
                        MAP_SHARED,
                        fd, HW_REGS_BASE);

    if (virtual_base == MAP_FAILED) return 1;

    h2p_lw_pwm_addr = virtual_base +
        ((unsigned long)(ALT_LWFPGASLVS_OFST + PIO_PWM_BASE) &
         (unsigned long)(HW_REGS_MASK));

    h2p_lw_accel_x_addr = virtual_base +
        ((unsigned long)(ALT_LWFPGASLVS_OFST + PIO_ACCEL_X_BASE) &
         (unsigned long)(HW_REGS_MASK));

    h2p_lw_accel_y_addr = virtual_base +
        ((unsigned long)(ALT_LWFPGASLVS_OFST + PIO_ACCEL_Y_BASE) &
         (unsigned long)(HW_REGS_MASK));

    h2p_lw_accel_z_addr = virtual_base +
        ((unsigned long)(ALT_LWFPGASLVS_OFST + PIO_ACCEL_Z_BASE) &
         (unsigned long)(HW_REGS_MASK));

    pthread_t http_thread;
    if (pthread_create(&http_thread, NULL, http_server_thread, NULL) == 0)
        pthread_detach(http_thread);

    printf("Starting accel->PWM control loop...\n");

    float prev_mag = 0.0f;
    float smooth_mag = 0.0f;
    int current_duty = 0;

    const float ALPHA = 0.1f;
    const float SCALE = 2000.0f;
    const int MAX_DUTY = 100;
    const int MIN_DUTY = 0;
    const int HYSTERESIS = 2;
    const int MAX_STEP = 2;

    unsigned int loop_count = 0;

    while (1) {
        int16_t ax = (int16_t)(*(volatile uint32_t *)h2p_lw_accel_x_addr & 0xFFFF);
        int16_t ay = (int16_t)(*(volatile uint32_t *)h2p_lw_accel_y_addr & 0xFFFF);
        int16_t az = (int16_t)(*(volatile uint32_t *)h2p_lw_accel_z_addr & 0xFFFF);

        g_ax = ax;
        g_ay = ay;
        g_az = az;

        float fx = (float)ax;
        float fy = (float)ay;
        float fz = (float)az;

        float mag = sqrtf(fx*fx + fy*fy + fz*fz);
        float diff = fabsf(mag - prev_mag);
        prev_mag = mag;

        smooth_mag = ALPHA * diff + (1.0f - ALPHA) * smooth_mag;

        float norm = smooth_mag / SCALE;
        if (norm > 1.0f) norm = 1.0f;
        if (norm < 0.0f) norm = 0.0f;

        int target_duty = (int)(norm * 100.0f);
        if (target_duty > MAX_DUTY) target_duty = MAX_DUTY;
        if (target_duty < MIN_DUTY) target_duty = MIN_DUTY;

        int delta = target_duty - current_duty;
        if (abs(delta) > HYSTERESIS) {
            if (delta > MAX_STEP) delta = MAX_STEP;
            else if (delta < -MAX_STEP) delta = -MAX_STEP;
            current_duty += delta;
        }

        *(volatile uint32_t *)h2p_lw_pwm_addr = (uint32_t)current_duty;
        g_duty = current_duty;

        if ((loop_count++ % 10) == 0)
            printf("AX=%6d AY=%6d AZ=%6d DUTY=%3d%%\n", ax, ay, az, current_duty);

        usleep(100 * 1000);
    }

    munmap(virtual_base, HW_REGS_SPAN);
    close(fd);
    return 0;
}

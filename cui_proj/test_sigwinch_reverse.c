#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pty.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <string.h>

int main() {
    int master;
    struct winsize ws_init = {20, 20, 0, 0}; // 20 rows, 20 cols
    pid_t pid = forkpty(&master, NULL, NULL, &ws_init);
    if (pid == 0) {
        execlp("bash", "bash", "--norc", "--noprofile", NULL);
    }
    
    // Read initial prompt
    char buf[1024];
    usleep(100000);
    read(master, buf, sizeof(buf));
    
    // Send some input that wraps
    write(master, "123456789012345", 15);
    usleep(100000);
    int n = read(master, buf, sizeof(buf)-1);
    buf[n] = 0;
    
    // Resize terminal width from 20 to 80
    struct winsize ws = {20, 80, 0, 0}; // 20 rows, 80 cols
    ioctl(master, TIOCSWINSZ, &ws);
    
    // See what bash outputs
    usleep(100000);
    n = read(master, buf, sizeof(buf)-1);
    if (n > 0) {
        buf[n] = 0;
        printf("Output on resize: ");
        for(int i=0; i<n; i++) {
            if (buf[i] == '\033') printf("\\033");
            else if (buf[i] == '\r') printf("\\r");
            else if (buf[i] == '\n') printf("\\n");
            else printf("%c", buf[i]);
        }
        printf("\n");
    } else {
        printf("No output on resize\n");
    }
}

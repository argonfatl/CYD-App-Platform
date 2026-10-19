#include <stdio.h>
#include <unistd.h>

#define CLOCK_DEMO_SECONDS 30

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    puts("\n[CYD Clock] Clock ELF module started.");
    puts("[CYD Clock] The launcher draws the elapsed-time UI on the display.");
    puts("[CYD Clock] This demo counter is elapsed time, not wall-clock time.");

    for (unsigned int elapsed = 0; elapsed < CLOCK_DEMO_SECONDS; ++elapsed) {
        printf("[CYD Clock] elapsed %02u:%02u:%02u\n",
               elapsed / 3600, (elapsed / 60) % 60, elapsed % 60);
        sleep(1);
    }

    puts("[CYD Clock] Demo complete; returning control to the launcher.\n");
    return 0;
}

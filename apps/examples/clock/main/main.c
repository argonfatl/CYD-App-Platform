#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    puts("\n[CYD Clock] Clock ELF module started.");
    puts("[CYD Clock] This first version shows elapsed runtime, not wall-clock time.");

    for (unsigned int elapsed = 0; elapsed <= 10; ++elapsed) {
        unsigned int hours = elapsed / 3600;
        unsigned int minutes = (elapsed % 3600) / 60;
        unsigned int seconds = elapsed % 60;

        printf("[CYD Clock] %02u:%02u:%02u\n", hours, minutes, seconds);

        if (elapsed < 10) {
            sleep(1);
        }
    }

    puts("[CYD Clock] Demo complete; returning control to the launcher.\n");
    return 0;
}

/* Disposable native title for the Task 1 registration/launch contract. */
#include <fcntl.h>
int sceKernelOpen(const char*, int, unsigned short);
long sceKernelWrite(int, const void*, unsigned long);
int sceKernelClose(int);
int sceKernelUsleep(unsigned);
int sceSystemServiceHideSplashScreen(void);
void _start(void* parameters, void* teardown) {
  (void)parameters; (void)teardown;
  const char proof[] = "Playden disposable native title started\n";
  int fd = sceKernelOpen("/app0/started.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd >= 0) { sceKernelWrite(fd, proof, sizeof(proof)-1); sceKernelClose(fd); }
  sceSystemServiceHideSplashScreen();
  for (;;) sceKernelUsleep(100000);
}

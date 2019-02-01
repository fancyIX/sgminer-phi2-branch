#ifndef OCL_H
#define OCL_H

#include <stdbool.h>

typedef struct __clState {
  cl_context context;
  cl_kernel kernel;
  cl_kernel *extra_kernels;
  cl_kernel GenerateDAG;

  /// mtp kernels
  cl_kernel mtp_0; //initialization
  cl_kernel mtp_1; //initialization
  cl_kernel mtp_2; //initialization
  cl_kernel mtp_3; //initialization
  cl_kernel mtp_fc; //initialization
  cl_kernel mtp_yloop; // main kernel

  size_t n_extra_kernels;
  cl_command_queue commandQueue;
  cl_program program;
  cl_mem outputBuffer;
  cl_mem CLbuffer0;
  cl_mem MidstateBuf;
  cl_mem MatrixBuf;
  cl_mem padbuffer8;
  cl_mem buffer1;
  cl_mem buffer2;
  cl_mem buffer3;
  unsigned char cldata[256];
  bool goffset;
  cl_uint vwidth;
  int devid;
  size_t max_work_size;
  size_t wsize;
  size_t compute_shaders;
  bool prebuilt;
} _clState;

extern int clDevicesNum(void);
extern _clState *initCl(unsigned int gpu, char *name, size_t nameSize, algorithm_t *algorithm);

#endif /* OCL_H */

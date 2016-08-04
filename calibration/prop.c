/*
MIT License
Copyright (c) 2015 Daniel Patrick Johnson <teknotus@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:
.
The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.
.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, 
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE 
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

/* compile using
gcc prop.c `pkg-config libusb-1.0 --libs --cflags`
*/

#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

typedef struct {
        float fx;
        float s;
        float cx;
        float i21;
        float fy;
        float cy;
        float i31;
        float i32;
        float i33;
} intrinsic_matrix;

typedef struct {
        float k1;
        float k2;
        float p1;
        float p2;
        float k3;
} dist_coeffs;

typedef struct {
        float r11;
        float r12;
        float r13;
        float r21;
        float r22;
        float r23;
        float r31;
        float r32;
        float r33;
} rotation_matrix;

typedef struct {
        float t1;
        float t2;
        float t3;
} translation_vector;

typedef struct {
        float r11;
        float r12;
        float r13;
        float t1;
        float r21;
        float r22;
        float r23;
        float t2;
        float r31;
        float r32;
        float r33;
        float t3;
} rotation_translation_matrix;

typedef struct fromCamera fromCamera;
struct fromCamera {
        float head[3];
        intrinsic_matrix im0;
        dist_coeffs dc0;
        dist_coeffs dc1;
        rotation_translation_matrix rt0;
        intrinsic_matrix im1;

        rotation_matrix rm0;
        translation_vector tv0;
        dist_coeffs dc2;
        dist_coeffs dc3;
        rotation_translation_matrix rt1;
        //float stuff1[34];
        intrinsic_matrix im2;
        rotation_matrix rm1;
        translation_vector tv1;
        dist_coeffs dc4;
        dist_coeffs dc5;
};
rotation_translation_matrix merge_rt(rotation_matrix, translation_vector);
void print_intrinsic(intrinsic_matrix);
void print_dist(dist_coeffs);
void print_rotation_translation(rotation_translation_matrix);

unsigned char get_temp(libusb_device_handle *);
uint16_t get_other(libusb_device_handle *);
void print_packet(libusb_device_handle *, char);
int get_packet(libusb_device_handle *, char, unsigned char *,int *);

int main(int argc, char *argv[]){
  int c, c_flag=0,p_flag=0;
  while ((c = getopt (argc, argv, "cp")) != -1){
    switch (c)
      {
      case 'c':
          c_flag=1;
          break;
      case 'p':
	  p_flag=1;
	  break;
      default:
        exit(0);
      }
  }	
  libusb_device **dev_list;
  libusb_device_handle *dh;
  libusb_context *context = NULL;
  int status,i,transferred;
  unsigned char buffer[1024];
  unsigned char temp = 0 , old_temp = 0;
  uint16_t other_temp = 0, old_other_temp = 0;
  ssize_t count;
  bool claimed = false;
  status = libusb_init(&context);
  if(status < 0) {
    fprintf(stderr,"libusb init error\n");
    return 1;
  }
  libusb_set_debug(context, 3);
  count = libusb_get_device_list(context, &dev_list);
  if(count < 0) {
    fprintf(stderr,"get device list error\n");
  }
  printf("// found %d devices\n", (int)count);
  for(i = 0 ; i < count ; i++){
    struct libusb_device_descriptor descriptor;
    int status = libusb_get_device_descriptor(dev_list[i], &descriptor);
    if(status < 0){
      fprintf(stderr, "error getting descriptor\n");
      return 1;
    }
    if(0x8086 == descriptor.idVendor && 0x0a66 == descriptor.idProduct){
      printf("// found realsense camera\n");
      status = libusb_open(dev_list[i], &dh);		
      if(status < 0){
	fprintf(stderr, "failed to open device\n");
	return 1;
      } else {
	status = libusb_claim_interface(dh,4);
	if(status < 0){
	  fprintf(stderr, "could not claim interface\n");
	  return 1;
	} else {
	  claimed = true;
	  break;
	}
      }
    }
  }
  if(claimed){
    if(p_flag){
      print_packet(dh,0x12);
      print_packet(dh,0x3b);
      print_packet(dh,0x3d);
      print_packet(dh,0x52);
      print_packet(dh,0x0a);
      exit(0);
    } else if(c_flag){
      get_packet(dh,0x3d,buffer,&transferred);
      exit(0);
    }
    for(;;){
      old_temp = temp;
      temp = get_temp(dh);
      if(temp != old_temp){
	time_t now = time (NULL);
	char * time_string = asctime (gmtime (&now));
	int len = strlen(time_string);
	if(time_string[len - 1] == '\n') time_string[len - 1]=0;
	printf("%s ",time_string);
	printf("Temperature: %d\n", temp);
      }
      usleep(30000);
    }
  }
  libusb_close(dh);
  libusb_free_device_list(dev_list, 1);
  libusb_exit(context);
  return 0;
}
unsigned char get_temp(libusb_device_handle *dh){
  int transferred,status;

  unsigned char data[24] = {
    0x14, 0x00, 0xab, 0xcd, 0x52, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  };
  unsigned char buffer[1024];
  status = libusb_bulk_transfer(dh,1,data,24,&transferred,0);
  if(status < 0){
    fprintf(stderr, "bulk out failed\n");
  }
  status = libusb_bulk_transfer(dh,0x81,buffer,1024,&transferred,0);
  if(status < 0){
    fprintf(stderr, "bulk in failed\n");
  } else {
    return buffer[4];
  }
}
uint16_t get_other(libusb_device_handle *dh){
  int transferred,status;
  uint16_t val = 0;

  unsigned char data[24] = {
    0x14, 0x00, 0xab, 0xcd, 0x0a, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  };
  unsigned char buffer[1024];
  status = libusb_bulk_transfer(dh,1,data,24,&transferred,0);
  if(status < 0){
    fprintf(stderr, "bulk out failed\n");
  }
  status = libusb_bulk_transfer(dh,0x81,buffer,1024,&transferred,0);
  if(status < 0){
    fprintf(stderr, "bulk in failed\n");
  } else {
    val = buffer[5] << 8;
    val |= buffer[6];
    return val;
  }
}
void print_packet(libusb_device_handle *dh,char id){
  int transferred,status,i;

  unsigned char data[24] = {
    0x14, 0x00, 0xab, 0xcd, 0x52, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  };
  data[4] = id;
  unsigned char buffer[1024];
  status = libusb_bulk_transfer(dh,1,data,24,&transferred,0);
  if(status < 0){
    fprintf(stderr, "bulk out failed\n");
  }
  status = libusb_bulk_transfer(dh,0x81,buffer,1024,&transferred,0);
  if(status < 0){
    fprintf(stderr, "bulk in failed\n");
  } else {
    printf("static const unsigned char pkt%02x[%d] = {\n",id,transferred);
    for(i=0;i<transferred;i++){
      printf("0x%02x",buffer[i]);
      if( i+1 == transferred ){
	printf("\n};\n");
      } else if( i % 8 == 7 ){
	printf(",\n");
      } else {
	printf(", ");
      }
    }
    return;
  }
}
int get_packet(libusb_device_handle *dh, char id, unsigned char * buffer,int *transferred){
  int status,i;

  unsigned char data[24] = {
    0x14, 0x00, 0xab, 0xcd, 0x52, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  };
  data[4] = id;
  //unsigned char buffer[1024];
  status = libusb_bulk_transfer(dh,1,data,24,transferred,0);
  if(status < 0){
    fprintf(stderr, "bulk out failed\n");
  }
  status = libusb_bulk_transfer(dh,0x81,buffer,1024,transferred,0);
  if(status < 0){
    fprintf(stderr, "bulk in failed\n");
  } else {
  int length_big = *transferred/4;
  //int start_32 = 512/32;
  float * nums_bigf = (float *)buffer;
  //nums_bigf += start_32;
  fromCamera * fc = (fromCamera *)nums_bigf;
  //intrinsic_matrix im = fc->im;
  printf("IR\n");
  print_intrinsic(fc->im0);
  print_dist(fc->dc0);
  print_dist(fc->dc1);
  print_rotation_translation(fc->rt0);
  printf("projector\n");

  print_intrinsic(fc->im1);
  print_dist(fc->dc2);
  print_dist(fc->dc3);
  print_rotation_translation(merge_rt(fc->rm0,fc->tv0));
  printf("texture\n");
  print_rotation_translation(fc->rt1);
  printf("color\n");
  print_intrinsic(fc->im2);
  print_dist(fc->dc4);
  print_dist(fc->dc5);
  print_rotation_translation(merge_rt(fc->rm1,fc->tv1));
    return status;
  }
}
rotation_translation_matrix merge_rt(rotation_matrix rm, translation_vector tv){
  rotation_translation_matrix rt;
  rt.r11 = rm.r11;
  rt.r12 = rm.r12;
  rt.r13 = rm.r13;
  rt.r21 = rm.r21;
  rt.r22 = rm.r22;
  rt.r23 = rm.r23;
  rt.r31 = rm.r31;
  rt.r32 = rm.r32;
  rt.r33 = rm.r33;
  rt.t1 = tv.t1;
  rt.t2 = tv.t2;
  rt.t3 = tv.t3;
  return rt;
}
void print_intrinsic(intrinsic_matrix im){
  printf("%11f %11f %11f\n%11f %11f %11f\n%11f %11f %11f\n\n",
        im.fx, im.s, im.cx, im.i21,im.fy,im.cy,im.i31,im.i32,im.i33);
}
void print_dist(dist_coeffs dc){
  printf("%11f %11f %11f %11f %11f\n\n",
        dc.k1, dc.k2, dc.p1, dc.p2, dc.k3);
}
void print_rotation_translation(rotation_translation_matrix rt){
  printf("%11f %11f %11f %11f\n%11f %11f %11f %11f\n%11f %11f %11f %11f\n\n",
        rt.r11, rt.r12, rt.r13, rt.t1,
        rt.r21, rt.r22, rt.r23, rt.t2,
        rt.r31, rt.r32, rt.r33, rt.t3);
}


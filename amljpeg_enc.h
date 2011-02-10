#ifndef AM_JPEG_ENCODER_9908049
#define AM_JPEG_ENCODER_9908049

typedef struct {
	int width;
	int height;
	int quality;
	
	unsigned char* idata;
	unsigned char* odata;
	int ibuff_size;
	int obuff_size;
	
} jpeg_enc_t;

extern int encode_jpeg(jpeg_enc_t* enc);

#endif /* AM_JPEG_ENCODER_9908049 */
#define LOG_NDEBUG 0
#define NDEBUG 0
#define LOG_TAG "OpVdin"
#include <utils/Log.h>
#include <sys/mman.h>


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include "tvin.h"


int global_w=1280,global_h=720;


#include "jpeglib.h"
#include "jconfig.h"

#include <setjmp.h>
#include "amljpeg_enc.h"
typedef struct {
  struct jpeg_destination_mgr pub; /* public fields */

  char* obuf;
  int buf_size;
  int data_size;
} aml_destination_mgr;

typedef aml_destination_mgr * aml_dest_ptr;

static void init_destination (j_compress_ptr cinfo)
{
  aml_dest_ptr dest = (aml_dest_ptr) cinfo->dest;

  dest->pub.next_output_byte = dest->obuf;
  dest->pub.free_in_buffer = dest->buf_size;
}

static boolean empty_output_buffer (j_compress_ptr cinfo)
{
  aml_dest_ptr dest = (aml_dest_ptr) cinfo->dest;

  /* error handling, input buffer too small. */
  /* maybe you need not use jpeg encoder. */

  return FALSE;
}

static void term_destination (j_compress_ptr cinfo)
{
  aml_dest_ptr dest = (aml_dest_ptr) cinfo->dest;
  size_t datacount = dest->buf_size - dest->pub.free_in_buffer;

  dest->data_size = datacount;
}

int encode_jpeg(jpeg_enc_t* enc) 
{

	/* create compress. information data. */
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	aml_destination_mgr dest;
	
	JSAMPROW row_pointer[1];	/* pointer to JSAMPLE row[s] */
	int row_stride;		/* physical row width in image buffer */
	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_compress(&cinfo);
	//jpeg_stdio_dest(&cinfo, outfile);
	cinfo.dest=(struct jpeg_destination_mgr *)&dest;
	dest.pub.init_destination = init_destination;
	dest.pub.empty_output_buffer = empty_output_buffer;
	dest.pub.term_destination = term_destination;
	
	dest.obuf = enc->odata;
	dest.buf_size = enc->obuff_size;

	/* set parameters. */
	cinfo.image_width = enc->width; 	
	cinfo.image_height = enc->height;
	cinfo.input_components = 3;		
	cinfo.in_color_space = JCS_RGB;
	jpeg_set_defaults(&cinfo);
	if(enc->quality==0)
		enc->quality=75;
	jpeg_set_quality(&cinfo, enc->quality, TRUE);

	jpeg_start_compress(&cinfo, TRUE);

	row_stride = enc->width * 3;	

	/* processing. */
	while (cinfo.next_scanline < cinfo.image_height) {
		row_pointer[0] = & enc->idata[cinfo.next_scanline * row_stride];
		(void) jpeg_write_scanlines(&cinfo, row_pointer, 1);
	}

	jpeg_finish_compress(&cinfo);
	jpeg_destroy_compress(&cinfo);
	return dest.data_size;
}



#include <cmem.h>
#include "ge2d.h"
#define CANVAS_ALIGNED(x)	(((x) + 7) & ~7)
CMEM_AllocParams cmemParm;
#define   FILE_NAME_GE2D		  "/dev/ge2d"

int cmem_init()
{
	cmemParm.type=CMEM_HEAP; cmemParm.flags=CMEM_NONCACHED; cmemParm.alignment=8;
	if(CMEM_init())
    {    
		LOGV("cmem init error\n");	
		return -1;	
    }
    return 0;
}

void cmem_exit()
{
	CMEM_exit();    
}
int GetCameraOutputData(char *buf,int dst_format)
{
	int fd_vdin, fd_ge2d;	
	struct tvin_parm_s src_mux_cfg;
	ge2d_op_para_t op_para={0};
	config_para_ex_t ge2d_config_ex={0};
	char *mp = NULL , *dst_plan =NULL;;
	
	fd_vdin = open("/dev/vdin0", O_RDWR);
	
	if (fd_vdin>=0)
	{	
		
		if(ioctl(fd_vdin, TVIN_IOC_G_PARM, &src_mux_cfg) < 0)
			LOGV("AMLOGIC CAMERA API: ioctl get parm error");
			
		if (src_mux_cfg.status != TVIN_SIG_STATUS_STABLE)
        {
        	//sleep(40); 	//delay 40ms wait for nex field	
        	usleep(40000); //delay 40ms wait for nex field
        	LOGV("AMLOGIC CAMERA API: status not stable");            
        }
                
		src_mux_cfg.flag |= TVIN_PARM_FLAG_CAP;
		
		if (ioctl(fd_vdin, TVIN_IOC_S_PARM, &src_mux_cfg) < 0)
			LOGV("AMLOGIC CAMERA API: ioctl set cap_falg error");	
			
		usleep(40000); 		
		//sleep(2);
		if(ioctl(fd_vdin, TVIN_IOC_G_PARM, &src_mux_cfg) < 0)
			LOGV("AMLOGIC CAMERA API: ioctl get cap parm error");				

		
		if (cmem_init()<0)
		{
			close(fd_vdin);
			return -1;
		}
	
	    fd_ge2d = open(FILE_NAME_GE2D, O_RDWR);
		if (fd_ge2d<0)
		{
			LOGV("AMLOGIC CAMERA API: open ge2d fail ");
			close(fd_vdin);
			return -1;	
		}
		if (dst_format==GE2D_FORMAT_S24_BGR)
	    	dst_plan=CMEM_alloc(0,global_w*global_h*3, &cmemParm);
	    else
	    	dst_plan=CMEM_alloc(0,global_w*global_h*2, &cmemParm);
	    if(!dst_plan) 
	    {
		    LOGV("AMLOGIC CAMERA API: CMEM_alloc error ");
			close(fd_ge2d);
			close(fd_vdin);
			return -1;
	    }
	    
	    mp = (char *)mmap(0, src_mux_cfg.cap_size, (PROT_WRITE | PROT_READ), MAP_SHARED, fd_vdin, 0);
	    
	    //LOGE("API: mmap return addr is:%x \n", (int)mp);
	    
	    if(MAP_FAILED == mp) 
	    {
	    	LOGV("AMLOGIC CAMERA API: mmap failed\n");        
	    	CMEM_free(dst_plan, &cmemParm); 		   
	        cmem_exit();  
	        close(fd_ge2d);
	        close(fd_vdin);
	        return -1;
	    }  
	    
	   // LOGV("AMLOGIC CAMERA API: write mmap OK ");  
		
	    ge2d_config_ex.alu_const_color = 0xff0000ff;
	    ge2d_config_ex.src1_gb_alpha = 0xff;
	    ge2d_config_ex.dst_xy_swap = 0;
	
	    ge2d_config_ex.src_para.canvas_index=(src_mux_cfg.canvas_index);
	    ge2d_config_ex.src_para.mem_type = TYPE_INVALID;
	    ge2d_config_ex.src_para.format  = GE2D_FORMAT_S16_YUV422;//GE2D_FORMAT_S16_YUV422;
	    ge2d_config_ex.src_para.x_rev = 0;
	    ge2d_config_ex.src_para.y_rev = 0;
	    ge2d_config_ex.src_para.color = 0xff;
	    ge2d_config_ex.src_para.top = 0;
	    ge2d_config_ex.src_para.left = 0;
	    ge2d_config_ex.src_para.width = global_w;/* buffer width. */
	    ge2d_config_ex.src_para.height = global_h;/* buffer height. */
	
	    ge2d_config_ex.src2_para.mem_type = CANVAS_TYPE_INVALID;
	
	    ge2d_config_ex.dst_para.mem_type = CANVAS_ALLOC;//CANVAS_OSD1;//CANVAS_ALLOC;//CANVAS_OSD0
	    ge2d_config_ex.dst_para.format  = dst_format;//GE2D_FORMAT_S16_RGB_565;//GE2D_FORMAT_S32_ARGB;//GE2D_FORMAT_S16_RGB_565;
	    ge2d_config_ex.dst_para.x_rev = 0;
	    ge2d_config_ex.dst_para.y_rev = 0;
	    ge2d_config_ex.dst_para.color = 0;
	    ge2d_config_ex.dst_para.top = 0;
	    ge2d_config_ex.dst_para.left = 0;
	    ge2d_config_ex.dst_para.width = global_w;/* buffer width. */;
	    ge2d_config_ex.dst_para.height =global_h; /* buffer height. */;
	    
	 	ge2d_config_ex.dst_planes[0].addr = CMEM_getPhys(dst_plan);
	    ge2d_config_ex.dst_planes[0].w = global_w;
	    ge2d_config_ex.dst_planes[0].h = global_h;  
		
	    ioctl(fd_ge2d, FBIOPUT_GE2D_CONFIG_EX, &ge2d_config_ex);
		
	    memset((char*)&op_para,0,sizeof(ge2d_op_para_t));
	    
	    op_para.src1_rect.x = 0;
	    op_para.src1_rect.y = 0;
	    op_para.src1_rect.w =global_w; /*input_image_width*/;
	    op_para.src1_rect.h =global_h; /*input_image_height*/;
	    op_para.dst_rect.x = 0;
	    op_para.dst_rect.y = 0;
	    op_para.dst_rect.w = global_w;/*output_image_width*/;
	    op_para.dst_rect.h = global_h;/*output_image_height*/;    
	    
	    ioctl(fd_ge2d, FBIOPUT_GE2D_STRETCHBLIT_NOALPHA, &op_para); 
	    	
		//LOGV("AMLOGIC CAMERA API: ge2d  ioctl 444");
			
		if (dst_format==GE2D_FORMAT_S24_BGR)
			memcpy(buf,dst_plan,global_w*global_h*3);
		else		
			memcpy(buf,dst_plan,global_w*global_h*2);
			
		CMEM_free(dst_plan, &cmemParm); 	 
		   
	    cmem_exit();  
	
	        
	    if (munmap(mp, src_mux_cfg.cap_size) < 0)
	       LOGV("API: nunmap error\n");  
	       
	    src_mux_cfg.flag &= ~TVIN_PARM_FLAG_CAP ;        
	    
	    if (ioctl(fd_vdin, TVIN_IOC_S_PARM, &src_mux_cfg) < 0)
			LOGV("AMLOGIC CAMERA API: ioctl set para error");
									
		close(fd_vdin);
		close(fd_ge2d);	
			
		return 0 ;
		
	}
	else
	{
		LOGV("AMLOGIC CAMERA vdin device open error !!");
		return -1;	
	}			

}



int Openvdin(void)
{
 	int vdin_fd, temp_index = -1;
	tvin_sig_fmt_t resolution_index = ConvertResToDriver(global_w,global_h,30);
	struct tvin_parm_s src_mux_cfg = {TVIN_PORT_CAMERA,resolution_index, TVIN_SIG_STATUS_NULL, 0, 0,0,0};
//	struct tvin_parm_s src_mux_cfg = {TVIN_PORT_BT656, TVIN_SIG_FMT_BT656IN_576I, TVIN_SIG_STATUS_NULL, 0x82000000, 0};
	/*open iotcl close*/
	vdin_fd = open("/dev/vdin0", O_RDWR);

	if(vdin_fd > 0)
	{	
		temp_index = ioctl(vdin_fd, TVIN_IOC_START_DEC, &src_mux_cfg);			
		close(vdin_fd);
		LOGV("AMLOGIC CAMERA vdin device open successful !!");	
		return 0;
	}
	else
	{
		LOGV("AMLOGIC CAMERA vdin device open error !!");
		return -1;
	}

}

int Stopvdin(void)
{
 	int vdin_fd, temp_index = -1;
	tvin_sig_fmt_t resolution_index = ConvertResToDriver(global_w,global_h,30);
	struct tvin_parm_s src_mux_cfg = {TVIN_PORT_CAMERA,resolution_index, TVIN_SIG_STATUS_NULL, 0x82000000,0, 0,0};
//	struct tvin_parm_s src_mux_cfg = {TVIN_PORT_BT656, TVIN_SIG_FMT_BT656IN_576I, TVIN_SIG_STATUS_NULL, 0x82000000, 0};
	/*open iotcl close*/
	vdin_fd = open("/dev/vdin0", O_RDWR);

	if(vdin_fd < 0)
	{
		LOGV("AMLOGIC CAMERA Stopvdin error !!");
		return -1;
	}	
	else
	{	
		temp_index = ioctl(vdin_fd, TVIN_IOC_STOP_DEC, &src_mux_cfg);			
		close(vdin_fd);
		LOGV("AMLOGIC CAMERA Stopvdin successful !!");	
		return 0;
	}

}

	


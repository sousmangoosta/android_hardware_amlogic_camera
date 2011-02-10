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

#define ENABLE_JPEG_DECODE		1

#if ENABLE_JPEG_DECODE

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

#endif

#if 1 
//for rgb565
/*
要想把从video buffer得到的数据转换成RGB565，利用android的APK流程显示在OSD层
需要做以下修改
1：
编译kernel时，make menuconfig [*]ge2d [M]cmem,make modules,把cmemk.ko考/system/lib/
2：
init.rc中insmod /system/lib/cmemk.ko  phys_start=0x85BF9000 phys_end=0x865F9000 allowOverlap=1

chmod 777 /dev/camera0
chmod 777 /dev/vdin0
chmod 777 /dev/ge2d

chmod 777 /dev/cmem ,先要查看是否该文件挂载成功，并且目前此句话目前可能在命令行输入，
修改CANVAS INDEX
 
		 reserve  osd1  osd2 (64M)
80000000+4000000+7F8000+1000        +16M(1000000)=857F9000
85BF9000 - 865F9000

增加拍照功能：
1:修改Android.mk，把libjpeg编译进来
2：CameraService.cpp中，registerPreviewBuffers（）handleShutter（）PIXEL_FORMAT_RGB_565


*/

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
	        close(fd_vdin);
	        return -1;
	    }  
	    
	   // LOGV("AMLOGIC CAMERA API: write mmap OK ");  
		
	    ge2d_config_ex.alu_const_color = 0xff0000ff;
	    ge2d_config_ex.src1_gb_alpha = 0xff;
	    ge2d_config_ex.dst_xy_swap = 0;
	
	    ge2d_config_ex.src_para.canvas_index=(src_mux_cfg.reserved+24);
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
			
		return 0 ;
		
	}
	else
	{
		LOGV("AMLOGIC CAMERA vdin device open error !!");
		return -1;	
	}			

}


#if 0//beifen
int GetCameraOutputData(char *buf)
{
	int fd ,fd_vdin,fd_osd ;	
	struct tvin_parm_s src_mux_cfg;
	char *mp = NULL;	

		
	//LOGV("AMLOGIC CAMERA GetFrameData !!");
	
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
			
		//for rgb565 start ************
		
		if (cmem_init()<0)
		{
			close(fd_vdin);
			return -1;
		}
	
		LOGV("AMLOGIC CAMERA API:start get rgb565 data "); 
		
	#if 0			
    	ge2d_op_para_t op_para={0};
    	config_para_t ge2d_config={0};
    	int w=1920,h=1080,fd_ge2d;    
    	fd_ge2d = open(FILE_NAME_GE2D, O_RDWR);
    	if (fd_ge2d<0)
    	{
    		LOGV("AMLOGIC CAMERA API: open ge2d fail ");
    		close(fd_vdin);
    		return -1;	
    	}
    	char * dst_plan=CMEM_alloc(0,w*h*2, &cmemParm);
    	if(!dst_plan) 
    	{
    		LOGV("AMLOGIC CAMERA API: CMEM_alloc error ");
    		close(fd_ge2d);
    		close(fd_vdin);
    		return -1;
    	}
    	
 /*   mp = (char *)mmap(0, src_mux_cfg.cap_size, (PROT_WRITE | PROT_READ), MAP_SHARED, fd_vdin, 0);
        
    if(MAP_FAILED == mp) 
    {
    	LOGV("AMLOGIC CAMERA API: mmap failed\n");        
        close(fd_vdin);
        return -1;
    }      
   static char val=0;
	if (val==0)
		val =255;
//	else if (val==125)
//		val=255;
	else
		val=0;
    memset(mp , val, 1920*1080*2);           
  */    
    //memcpy(buf,(char*)(((int)mp+1024*4)&0xfffff000),1024*3*320);
	//LOGV("AMLOGIC CAMERA API: write mmap OK ");

	    ge2d_config.src_dst_type = ALLOC_ALLOC;
		ge2d_config.alu_const_color=0xff0000ff;
		ge2d_config.src_format = GE2D_FORMAT_S16_RGB_565;
		ge2d_config.dst_format = GE2D_FORMAT_S16_RGB_565;//GE2D_FORMAT_S16_RGB_565;
	
		ge2d_config.src_planes[0].addr = src_mux_cfg.cap_addr;
		ge2d_config.src_planes[0].w = w;
		ge2d_config.src_planes[0].h = h;
		ge2d_config.dst_planes[0].addr = CMEM_getPhys(dst_plan);	
	    ge2d_config.dst_planes[0].w = w ;
		ge2d_config.dst_planes[0].h = h;
		
		LOGE(" src_mux_cfg.cap_addr = (%x)",  src_mux_cfg.cap_addr);
		LOGV("AMLOGIC CAMERA API: ge2d  ioctl 111");
		ioctl(fd_ge2d, FBIOPUT_GE2D_CONFIG, &ge2d_config);
	    LOGV("AMLOGIC CAMERA API: ge2d  ioctl 222");
		op_para.src1_rect.x = 0;
		op_para.src1_rect.y = 0;
		op_para.src1_rect.w = 800;//w;
		op_para.src1_rect.h = 600;//h;
		op_para.dst_rect.x = 0;
		op_para.dst_rect.y = 0;
		op_para.dst_rect.w = 800;//w;
		op_para.dst_rect.h = 600;//h;
		LOGV("AMLOGIC CAMERA API: ge2d  ioctl 333");
		ioctl(fd_ge2d, FBIOPUT_GE2D_STRETCHBLIT_NOALPHA, &op_para);	
        LOGV("AMLOGIC CAMERA API: ge2d  ioctl 444");
        
        memcpy(buf,dst_plan,1280*720*2); 
                
        LOGV("AMLOGIC CAMERA API:get rgb565 data end "); 
         
        CMEM_free(dst_plan, &cmemParm);
        
  		cmem_exit();  
  	#endif
#if 1
    ge2d_op_para_t op_para={0};
    config_para_ex_t ge2d_config_ex={0};
    int w=1920,h=1080,fd_ge2d;
    fd_ge2d = open(FILE_NAME_GE2D, O_RDWR);
	if (fd_ge2d<0)
	{
		LOGV("AMLOGIC CAMERA API: open ge2d fail ");
		close(fd_vdin);
		return -1;	
	}
    char * dst_plan=CMEM_alloc(0,1920*1080*2, &cmemParm);
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
        close(fd_vdin);
        return -1;
    }   
   static char val=0,count=0,ret;
   int fd1=0,i;
	/*if (val==0)
		val =255;
//	else if (val==125)
//		val=255;
	else
		val=0;
		
    memset(mp , 255, 1280*720*2);
    */
    count++;
    if (count==1)    
    {
		FILE* file ;
		file = fopen("/dat2/111.txt", "wt+");
		if(file)
		{
			ret = fwrite(mp,1,1280*720*2,file);
			if(ret != 1280*720*2)
			{
				LOGV("1_111  write length error");
				ret = 0 ;
			} 
			fclose(file);
		} 
		else 
		{
			LOGV("111  create file error");
			LOGE("create file error: (%d)", errno);			         
		}
		/*fd1 = open("/mnt/sdcard/111.txt",O_RDWR|O_CREAT);
		if( fd1<0)
		{			
			LOGV("11111  create file error");
		}
    	else
    	{
    	  lseek(fd1,0,SEEK_SET);
    	  write(fd1, mp,1280*720*2);
    	  close(fd1);
    	}   */ 	

    }
   else if (count==20) 
    {
    	
		FILE* file ;
		file = fopen("/dat2/222.txt", "wt+");
		if(file>=0)
		{
			ret = fwrite(mp,1,1280*720*2,file);
			if(ret != 1280*720*2)
			{
				LOGV("1_222  write length error");
				ret = 0 ;
			} 
			fclose(file);
		} 
		else 
		{
			LOGV("1_222  create file error");
		} 
    }
    //memcpy(buf,(char*)(((int)mp+1024*4)&0xfffff000),1024*3*320);
    
   // LOGV("AMLOGIC CAMERA API: write mmap OK ");    

	
    ge2d_config_ex.alu_const_color = 0xff0000ff;
    ge2d_config_ex.src1_gb_alpha = 0xff;
    ge2d_config_ex.dst_xy_swap = 0;

    ge2d_config_ex.src_para.canvas_index=(src_mux_cfg.reserved+24);
    ge2d_config_ex.src_para.mem_type = TYPE_INVALID;
    ge2d_config_ex.src_para.format  = GE2D_FORMAT_S16_YUV422;//GE2D_FORMAT_S16_YUV422;
    ge2d_config_ex.src_para.x_rev = 0;
    ge2d_config_ex.src_para.y_rev = 0;
    ge2d_config_ex.src_para.color = 0xff;
    ge2d_config_ex.src_para.top = 0;
    ge2d_config_ex.src_para.left = 0;
    ge2d_config_ex.src_para.width = w;/* buffer width. */
    ge2d_config_ex.src_para.height = h;/* buffer height. */

    ge2d_config_ex.src2_para.mem_type = CANVAS_TYPE_INVALID;

    ge2d_config_ex.dst_para.mem_type = CANVAS_ALLOC;//CANVAS_OSD1;//CANVAS_ALLOC;//CANVAS_OSD0
    ge2d_config_ex.dst_para.format  = GE2D_FORMAT_S16_RGB_565;//GE2D_FORMAT_S32_ARGB;//GE2D_FORMAT_S16_RGB_565;
    ge2d_config_ex.dst_para.x_rev = 0;
    ge2d_config_ex.dst_para.y_rev = 0;
    ge2d_config_ex.dst_para.color = 0;
    ge2d_config_ex.dst_para.top = 0;
    ge2d_config_ex.dst_para.left = 0;
    ge2d_config_ex.dst_para.width = 1280;/* buffer width. */;
    ge2d_config_ex.dst_para.height =720; /* buffer height. */;
    
 	ge2d_config_ex.dst_planes[0].addr = CMEM_getPhys(dst_plan);
    ge2d_config_ex.dst_planes[0].w = 1280;
    ge2d_config_ex.dst_planes[0].h = 720;    
	//LOGE(" src_mux_cfg.cap_addr = (%x)",  src_mux_cfg.cap_addr);
	//LOGV("AMLOGIC CAMERA API: ge2d  ioctl 111");
	
    ioctl(fd_ge2d, FBIOPUT_GE2D_CONFIG_EX, &ge2d_config_ex);
    
	//LOGV("AMLOGIC CAMERA API: ge2d  ioctl 222");
	
    memset((char*)&op_para,0,sizeof(ge2d_op_para_t));
    
    op_para.src1_rect.x = 0;
    op_para.src1_rect.y = 0;
    op_para.src1_rect.w =1280; /*input_image_width*/;
    op_para.src1_rect.h =720; /*input_image_height*/;
    op_para.dst_rect.x = 0;
    op_para.dst_rect.y = 0;
    op_para.dst_rect.w = 1280;/*output_image_width*/;
    op_para.dst_rect.h = 720;/*output_image_height*/;
    LOGV("AMLOGIC CAMERA API: ge2d  ioctl 333");
    
    ioctl(fd_ge2d, FBIOPUT_GE2D_STRETCHBLIT_NOALPHA, &op_para); 
    	
	LOGV("AMLOGIC CAMERA API: ge2d  ioctl 444");
	/* add your memcpy code here. */
	
	//fd_osd = open("/dev/graphics/fb1", O_RDWR);
    if (count==1)    
    {		
		FILE* file ;
		file = fopen("/dat2/aaa.txt", "wt+");
		if(file>=0)
		{
			ret = fwrite(dst_plan,1,1280*720*2,file);
			if(ret != 1280*720*2)
			{
				LOGV("1_aaa  write length error");
				ret = 0 ;
			} 
			fclose(file);
		} 
		else 
		{
			LOGV("aaa  create file error");

		}    	
    }
 else if (count==20) 
    {
    	
		FILE* file ;
		file = fopen("/dat2/bbb.txt", "wt+");
		if(file>=0)
		{
			ret = fwrite(dst_plan,1,1280*720*2,file);
			if(ret != 1280*720*2)
			{
				LOGV("1_bbb  write length error");
				ret = 0 ;
			} 
			fclose(file);
		} 
		else 
		{
			LOGV("1_bbb  create file error");
		} 
    }
	memcpy(buf,dst_plan,1280*720*2);
		
	CMEM_free(dst_plan, &cmemParm); 	 
	   
       cmem_exit();  
        //for rgb565 end *************
#endif

        
   if (munmap(mp, src_mux_cfg.cap_size) < 0)
       LOGV("API: nunmap error\n");  
       
        src_mux_cfg.flag &= ~TVIN_PARM_FLAG_CAP ;        
        
        if (ioctl(fd_vdin, TVIN_IOC_S_PARM, &src_mux_cfg) < 0)
			LOGV("AMLOGIC CAMERA API: ioctl set para error");
									
		close(fd_vdin);	
			
		return 0 ;
		
	}
	else
	{
		LOGV("AMLOGIC CAMERA vdin device open error !!");
		return -1;	
	}			

	//LOGE("open /dev/mem with fcntl(): %s (%d)", 
            // strerror(errno), errno);
}
#endif
#endif

int Openvdin(void)
{
 	int vdin_fd, temp_index = -1;

	struct tvin_parm_s src_mux_cfg = {TVIN_PORT_CAMERA,TVIN_SIG_FMT_CAMERA_1280X720P_30Hz, TVIN_SIG_STATUS_NULL, 0, 0,0,0};
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

	struct tvin_parm_s src_mux_cfg = {TVIN_PORT_CAMERA,TVIN_SIG_FMT_CAMERA_1280X720P_30Hz, TVIN_SIG_STATUS_NULL, 0x82000000,0, 0,0};
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
/*
  SEEK_SET： 0
  SEEK_CUR： 1
  SEEK_END： 2
*/
int GetFrameData(char *buf)
{
	int fd ,fd_vdin ;	
	struct tvin_parm_s src_mux_cfg;
	char *mp = NULL;	
		
	//LOGV("AMLOGIC CAMERA GetFrameData !!");
	
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

        mp = (char *)mmap(0, src_mux_cfg.cap_size, (PROT_WRITE | PROT_READ), MAP_SHARED, fd_vdin, 0);
        
        //LOGE("API: mmap return addr is:%x \n", (int)mp);
        
        if(MAP_FAILED == mp) 
        {
        	LOGV("AMLOGIC CAMERA API: mmap failed\n");        
            close(fd_vdin);
            return -1;
        }
        
        //LOGE("API: memcpy  addr is:%x \n", (int)buf);            
        //write data into the buffer

        memcpy(buf,mp,1280*720*2); 

        //memcpy(buf,(char*)(((int)mp+1024*4)&0xfffff000),1024*3*320);
        
        LOGV("AMLOGIC CAMERA API: write mmap OK ");
        
        //LOGV("AMLOGIC CAMERA API: memcpy ");
        
        if (munmap(mp, src_mux_cfg.cap_size) < 0)
            LOGV("API: nunmap error\n");   
         	
        src_mux_cfg.flag &= ~TVIN_PARM_FLAG_CAP ;        
        
        if (ioctl(fd_vdin, TVIN_IOC_S_PARM, &src_mux_cfg) < 0)
			LOGV("AMLOGIC CAMERA API: ioctl set para error");
									
		close(fd_vdin);	
			
		return 0 ;
		
	}
	else
	{
		LOGV("AMLOGIC CAMERA vdin device open error !!");
		return -1;	
	}			

	//LOGE("open /dev/mem with fcntl(): %s (%d)", 
            // strerror(errno), errno);
}
	


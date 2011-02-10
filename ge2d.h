/****************************************************************************
**
** Copyright (C) 2010 AMLOGIC, INC.
** All rights reserved.
****************************************************************************/

#ifndef GE2D_H
#define GE2D_H

#define  FBIOPUT_GE2D_STRETCHBLIT_NOALPHA   0x4702
#define  FBIOPUT_GE2D_BLIT_NOALPHA			0x4701
#define  FBIOPUT_GE2D_BLEND					0x4700
#define  FBIOPUT_GE2D_BLIT					0x46ff
#define  FBIOPUT_GE2D_STRETCHBLIT   		0x46fe
#define  FBIOPUT_GE2D_FILLRECTANGLE 		0x46fd
#define  FBIOPUT_GE2D_SRCCOLORKEY   		0x46fc
#define  FBIOPUT_GE2D_SET_COEF			0x46fb
#define  FBIOPUT_GE2D_CONFIG_EX			0x46fa
#define  FBIOPUT_GE2D_CONFIG				0x46f9
#define  FBIOPUT_GE2D_ANTIFLICKER_ENABLE    0x46f8 
typedef enum  	
{
	OSD0_OSD0 = 0,
	OSD0_OSD1,	 
	OSD1_OSD1,
	OSD1_OSD0,
	ALLOC_OSD0,
	ALLOC_OSD1,
	ALLOC_ALLOC,
	TYPE_INVALID,
} ge2d_src_dst_t;


typedef enum  	
{
    CANVAS_OSD0 =0,
    CANVAS_OSD1,	 
    CANVAS_ALLOC,
    CANVAS_TYPE_INVALID,
}ge2d_src_canvas_type;

typedef struct {
	unsigned long  addr;
	unsigned int	w;
	unsigned int	h;
}config_planes_t;

typedef    struct {
	int  src_dst_type;
	int  alu_const_color;
	unsigned int src_format;
	unsigned int dst_format ; //add for src&dst all in user space.

	config_planes_t src_planes[4];
	config_planes_t dst_planes[4];
}config_para_t;

typedef struct {
	int	x;   /* X coordinate of its top-left point */
	int	y;   /* Y coordinate of its top-left point */
	int	w;   /* width of it */
	int	h;   /* height of it */
} rectangle_t;

typedef  struct {
	unsigned int color ;
	rectangle_t src1_rect;
	rectangle_t src2_rect;
	rectangle_t	dst_rect;
	int op;
} ge2d_op_para_t ;

typedef    struct {
    int key_enable;
    int key_mode;
    int key_mask;
    int key;      
}key_ctrl_ex_t;

typedef  struct  {
    int  canvas_index;
    int  top;
    int  left;
    int  width;
    int  height;
    int  format;
    int  mem_type;
    int  color;
    unsigned char x_rev;
    unsigned char y_rev;
    unsigned char fill_color_en;
    unsigned char fill_mode;    
}src_dst_para_ex_t ;

typedef    struct {
    src_dst_para_ex_t src_para;
    src_dst_para_ex_t src2_para;
    src_dst_para_ex_t dst_para;

//key mask
    key_ctrl_ex_t  src_key;
    key_ctrl_ex_t  src2_key;

    int alu_const_color;
    unsigned src1_gb_alpha;
    unsigned op_mode;
    unsigned char bitmask_en;
    unsigned char bytemask_only;
    unsigned int  bitmask;
    unsigned char dst_xy_swap;

// scaler and phase releated
    unsigned hf_init_phase;
    int hf_rpt_num;
    unsigned hsc_start_phase_step;
    int hsc_phase_slope;
    unsigned vf_init_phase;
    int vf_rpt_num;
    unsigned vsc_start_phase_step;
    int vsc_phase_slope;
    unsigned char src1_vsc_phase0_always_en;
    unsigned char src1_hsc_phase0_always_en;
    unsigned char src1_hsc_rpt_ctrl;  //1bit, 0: using minus, 1: using repeat data
    unsigned char src1_vsc_rpt_ctrl;  //1bit, 0: using minus  1: using repeat data

//canvas info
    config_planes_t src_planes[4];
    config_planes_t src2_planes[4];
    config_planes_t dst_planes[4];
}config_para_ex_t;

#define GE2D_ENDIAN_SHIFT		24
#define GE2D_ENDIAN_MASK		(0x1 << GE2D_ENDIAN_SHIFT)
#define GE2D_BIG_ENDIAN			(0 << GE2D_ENDIAN_SHIFT)
#define GE2D_LITTLE_ENDIAN		(1 << GE2D_ENDIAN_SHIFT)

#define GE2D_COLOR_MAP_SHIFT	20
#define GE2D_COLOR_MAP_MASK		(0xf << GE2D_COLOR_MAP_SHIFT)
/* 16 bit */
#define GE2D_COLOR_MAP_YUV422	(0 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_RGB655	(1 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_YUV655	(1 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_RGB844	(2 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_YUV844	(2 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_RGBA6442	(3 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_YUVA6442	(3 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_RGBA4444	(4 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_YUVA4444	(4 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_RGB565	(5 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_YUV565	(5 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_ARGB4444	(6 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_AYUV4444	(6 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_ARGB1555	(7 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_AYUV1555	(7 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_RGBA4642	(8 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_YUVA4642	(8 << GE2D_COLOR_MAP_SHIFT)
/* 24 bit */
#define GE2D_COLOR_MAP_RGB888	(0 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_YUV444	(0 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_RGBA5658	(1 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_YUVA5658	(1 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_ARGB8565	(2 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_AYUV8565	(2 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_RGBA6666	(3 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_YUVA6666	(3 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_ARGB6666	(4 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_AYUV6666	(4 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_BGR888	(5 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_VUY888	(5 << GE2D_COLOR_MAP_SHIFT)
/* 32 bit */
#define GE2D_COLOR_MAP_RGBA8888	(0 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_YUVA8888	(0 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_ARGB8888	(1 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_AYUV8888	(1 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_ABGR8888	(2 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_AVUY8888	(2 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_BGRA8888	(3 << GE2D_COLOR_MAP_SHIFT)
#define GE2D_COLOR_MAP_VUYA8888	(3 << GE2D_COLOR_MAP_SHIFT)

#define GE2D_FMT_S8_Y			0x00000 /* 00_00_0_00_0_00 */
#define GE2D_FMT_S8_CB			0x00040 /* 00_01_0_00_0_00 */
#define GE2D_FMT_S8_CR			0x00080 /* 00_10_0_00_0_00 */
#define GE2D_FMT_S8_R			0x00000 /* 00_00_0_00_0_00 */
#define GE2D_FMT_S8_G			0x00040 /* 00_01_0_00_0_00 */
#define GE2D_FMT_S8_B			0x00080 /* 00_10_0_00_0_00 */
#define GE2D_FMT_S8_A			0x000c0 /* 00_11_0_00_0_00 */
#define GE2D_FMT_S8_LUT			0x00020 /* 00_00_1_00_0_00 */
#define GE2D_FMT_S16_YUV422		0x20102 /* 01_00_0_00_0_00 */
#define GE2D_FMT_S16_RGB		(GE2D_LITTLE_ENDIAN|0x00100) /* 01_00_0_00_0_00 */
#define GE2D_FMT_S24_YUV444		0x20200 /* 10_00_0_00_0_00 */
#define GE2D_FMT_S24_RGB		(GE2D_LITTLE_ENDIAN|0x00200) /* 10_00_0_00_0_00 */
#define GE2D_FMT_S32_YUVA444	0x20300 /* 11_00_0_00_0_00 */
#define GE2D_FMT_S32_RGBA		(GE2D_LITTLE_ENDIAN|0x00300) /* 11_00_0_00_0_00 */
#define GE2D_FMT_M24_YUV420		0x20007 /* 00_00_0_00_1_11 */
#define GE2D_FMT_M24_YUV422		0x20006 /* 00_00_0_00_1_10 */
#define GE2D_FMT_M24_YUV444		0x20004 /* 00_00_0_00_1_00 */
#define GE2D_FMT_M24_RGB		0x00004 /* 00_00_0_00_1_00 */
#define GE2D_FMT_M24_YUV420T	0x20017 /* 00_00_0_10_1_11 */
#define GE2D_FMT_M24_YUV420B	0x2001f /* 00_00_0_11_1_11 */
#define GE2D_FMT_S16_YUV422T	0x20110 /* 01_00_0_10_0_00 */
#define GE2D_FMT_S16_YUV422B	0x20138 /* 01_00_0_11_0_00 */
#define GE2D_FMT_S24_YUV444T	0x20210 /* 10_00_0_10_0_00 */
#define GE2D_FMT_S24_YUV444B	0x20218 /* 10_00_0_11_0_00 */

#define GE2D_FORMAT_S8_Y			(GE2D_FMT_S8_Y)           
#define GE2D_FORMAT_S8_CB			(GE2D_FMT_S8_CB)           
#define GE2D_FORMAT_S8_CR			(GE2D_FMT_S8_CR)           
#define GE2D_FORMAT_S8_R			(GE2D_FMT_S8_R)           
#define GE2D_FORMAT_S8_G			(GE2D_FMT_S8_G)           
#define GE2D_FORMAT_S8_B			(GE2D_FMT_S8_B)           
#define GE2D_FORMAT_S8_A			(GE2D_FMT_S8_A)           
#define GE2D_FORMAT_S8_LUT			(GE2D_FMT_S8_LUT)         
#define GE2D_FORMAT_S16_YUV422		(GE2D_FMT_S16_YUV422	| GE2D_COLOR_MAP_YUV422)
#define GE2D_FORMAT_S16_RGB_655		(GE2D_FMT_S16_RGB		| GE2D_COLOR_MAP_RGB655)  
#define GE2D_FORMAT_S16_RGB_565		(GE2D_FMT_S16_RGB		| GE2D_COLOR_MAP_RGB565) 
#define GE2D_FORMAT_S16_RGB_844		(GE2D_FMT_S16_RGB		| GE2D_COLOR_MAP_RGB844) 
#define GE2D_FORMAT_S16_RGBA_6442	(GE2D_FMT_S16_RGB		| GE2D_COLOR_MAP_RGBA6442)
#define GE2D_FORMAT_S16_RGBA_4444	(GE2D_FMT_S16_RGB		| GE2D_COLOR_MAP_RGBA4444)
#define GE2D_FORMAT_S16_ARGB_4444	(GE2D_FMT_S16_RGB		| GE2D_COLOR_MAP_ARGB4444)
#define GE2D_FORMAT_S16_ARGB_1555	(GE2D_FMT_S16_RGB		| GE2D_COLOR_MAP_ARGB1555)
#define GE2D_FORMAT_S16_RGBA_4642	(GE2D_FMT_S16_RGB		| GE2D_COLOR_MAP_RGBA4642)
#define GE2D_FORMAT_S24_YUV444		(GE2D_FMT_S24_YUV444	| GE2D_COLOR_MAP_YUV444) 
#define GE2D_FORMAT_S24_RGB			(GE2D_FMT_S24_RGB		| GE2D_COLOR_MAP_RGB888)   
#define GE2D_FORMAT_S32_YUVA444		(GE2D_FMT_S32_YUVA444	| GE2D_COLOR_MAP_YUVA4444)   
#define GE2D_FORMAT_S32_RGBA		(GE2D_FMT_S32_RGBA		| GE2D_COLOR_MAP_RGBA8888) 
#define GE2D_FORMAT_S32_ARGB		(GE2D_FMT_S32_RGBA		| GE2D_COLOR_MAP_ARGB8888) 
#define GE2D_FORMAT_S32_ABGR		(GE2D_FMT_S32_RGBA		| GE2D_COLOR_MAP_ABGR8888) 
#define GE2D_FORMAT_S32_BGRA		(GE2D_FMT_S32_RGBA		| GE2D_COLOR_MAP_BGRA8888) 
#define GE2D_FORMAT_S24_RGBA_5658	(GE2D_FMT_S24_RGB		| GE2D_COLOR_MAP_RGBA5658)  
#define GE2D_FORMAT_S24_ARGB_8565	(GE2D_FMT_S24_RGB		| GE2D_COLOR_MAP_ARGB8565) 
#define GE2D_FORMAT_S24_RGBA_6666	(GE2D_FMT_S24_RGB		| GE2D_COLOR_MAP_RGBA6666)
#define GE2D_FORMAT_S24_ARGB_6666	(GE2D_FMT_S24_RGB		| GE2D_COLOR_MAP_ARGB6666)
#define GE2D_FORMAT_S24_BGR			(GE2D_FMT_S24_RGB		| GE2D_COLOR_MAP_BGR888)
#define GE2D_FORMAT_M24_YUV420		(GE2D_FMT_M24_YUV420)   
#define GE2D_FORMAT_M24_YUV422		(GE2D_FMT_M24_YUV422)
#define GE2D_FORMAT_M24_YUV444		(GE2D_FMT_M24_YUV444)
#define GE2D_FORMAT_M24_RGB			(GE2D_FMT_M24_RGB)
#define GE2D_FORMAT_M24_YUV420T		(GE2D_FMT_M24_YUV420T)
#define GE2D_FORMAT_M24_YUV420B		(GE2D_FMT_M24_YUV420B)
#define GE2D_FORMAT_S16_YUV422T		(GE2D_FMT_S16_YUV422T | GE2D_COLOR_MAP_YUV422)
#define GE2D_FORMAT_S16_YUV422B		(GE2D_FMT_S16_YUV422B | GE2D_COLOR_MAP_YUV422)   
#define GE2D_FORMAT_S24_YUV444T		(GE2D_FMT_S24_YUV444T | GE2D_COLOR_MAP_YUV444)   
#define GE2D_FORMAT_S24_YUV444B		(GE2D_FMT_S24_YUV444B | GE2D_COLOR_MAP_YUV444)


#define OPERATION_ADD           0    //Cd = Cs*Fs+Cd*Fd
#define OPERATION_SUB           1    //Cd = Cs*Fs-Cd*Fd
#define OPERATION_REVERSE_SUB   2    //Cd = Cd*Fd-Cs*Fs
#define OPERATION_MIN           3    //Cd = Min(Cd*Fd,Cs*Fs)
#define OPERATION_MAX           4    //Cd = Max(Cd*Fd,Cs*Fs)
#define OPERATION_LOGIC         5

#define COLOR_FACTOR_ZERO                     0
#define COLOR_FACTOR_ONE                      1
#define COLOR_FACTOR_SRC_COLOR                2
#define COLOR_FACTOR_ONE_MINUS_SRC_COLOR      3
#define COLOR_FACTOR_DST_COLOR                4
#define COLOR_FACTOR_ONE_MINUS_DST_COLOR      5
#define COLOR_FACTOR_SRC_ALPHA                6
#define COLOR_FACTOR_ONE_MINUS_SRC_ALPHA      7
#define COLOR_FACTOR_DST_ALPHA                8
#define COLOR_FACTOR_ONE_MINUS_DST_ALPHA      9
#define COLOR_FACTOR_CONST_COLOR              10
#define COLOR_FACTOR_ONE_MINUS_CONST_COLOR    11
#define COLOR_FACTOR_CONST_ALPHA              12
#define COLOR_FACTOR_ONE_MINUS_CONST_ALPHA    13
#define COLOR_FACTOR_SRC_ALPHA_SATURATE       14

#define ALPHA_FACTOR_ZERO                     0
#define ALPHA_FACTOR_ONE                      1
#define ALPHA_FACTOR_SRC_ALPHA                2
#define ALPHA_FACTOR_ONE_MINUS_SRC_ALPHA      3
#define ALPHA_FACTOR_DST_ALPHA                4
#define ALPHA_FACTOR_ONE_MINUS_DST_ALPHA      5
#define ALPHA_FACTOR_CONST_ALPHA              6
#define ALPHA_FACTOR_ONE_MINUS_CONST_ALPHA    7

#define LOGIC_OPERATION_CLEAR       0
#define LOGIC_OPERATION_COPY        1
#define LOGIC_OPERATION_NOOP        2
#define LOGIC_OPERATION_SET         3
#define LOGIC_OPERATION_COPY_INVERT 4
#define LOGIC_OPERATION_INVERT      5
#define LOGIC_OPERATION_AND_REVERSE 6
#define LOGIC_OPERATION_OR_REVERSE  7
#define LOGIC_OPERATION_AND         8
#define LOGIC_OPERATION_OR          9
#define LOGIC_OPERATION_NAND        10
#define LOGIC_OPERATION_NOR         11
#define LOGIC_OPERATION_XOR         12
#define LOGIC_OPERATION_EQUIV       13
#define LOGIC_OPERATION_AND_INVERT  14
#define LOGIC_OPERATION_OR_INVERT   15 

static inline unsigned blendop(unsigned color_blending_mode,
                               unsigned color_blending_src_factor,
                               unsigned color_blending_dst_factor,
                               unsigned alpha_blending_mode,
                               unsigned alpha_blending_src_factor,
                               unsigned alpha_blending_dst_factor)
{
    return (color_blending_mode << 24) |
           (color_blending_src_factor << 20) |
           (color_blending_dst_factor << 16) |
           (alpha_blending_mode << 8) |
           (alpha_blending_src_factor << 4) |
           (alpha_blending_dst_factor << 0);
}


#endif /* GE2D_H */

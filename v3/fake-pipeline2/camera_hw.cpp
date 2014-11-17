#ifndef __CAMERA_HW__
#define __CAMERA_HW__

//#define LOG_NDEBUG 0
#define LOG_TAG "Camera_hw"

#include <errno.h>
#include "camera_hw.h"

#ifdef __cplusplus
//extern "C" {
#endif
static int set_rotate_value(int camera_fd, int value)
{
    int ret = 0;
    struct v4l2_control ctl;
    if(camera_fd<0)
        return -1;
    if((value!=0)&&(value!=90)&&(value!=180)&&(value!=270)){
        CAMHAL_LOGDB("Set rotate value invalid: %d.", value);
        return -1;
    }
    memset( &ctl, 0, sizeof(ctl));
    ctl.value=value;
	ctl.id = V4L2_CID_ROTATE;
	ALOGD("set_rotate_value:: id =%x , value=%d",ctl.id,ctl.value);
    ret = ioctl(camera_fd, VIDIOC_S_CTRL, &ctl);
    if(ret<0){
        CAMHAL_LOGDB("Set rotate value fail: %s,errno=%d. ret=%d", strerror(errno),errno,ret);
    }
    return ret ;
}

int camera_open(struct VideoInfo *cam_dev)
{
        char dev_name[128];
        int ret;

        sprintf(dev_name, "%s%d", "/dev/video", cam_dev->idx);
        cam_dev->fd = open(dev_name, O_RDWR | O_NONBLOCK);
        //cam_dev->fd = open("/dev/video0", O_RDWR | O_NONBLOCK);
        if (cam_dev->fd < 0){
                DBG_LOGB("open %s failed, errno=%d\n", dev_name, errno);
                return -ENOTTY;
        }

        ret = ioctl(cam_dev->fd, VIDIOC_QUERYCAP, &cam_dev->cap);
        if (ret < 0) {
                DBG_LOGB("VIDIOC_QUERYCAP, errno=%d", errno);
        }

        if (!(cam_dev->cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
                DBG_LOGB( "%s is not video capture device\n",
                                dev_name);
        }

        if (!(cam_dev->cap.capabilities & V4L2_CAP_STREAMING)) {
                DBG_LOGB( "video%d does not support streaming i/o\n",
                                cam_dev->idx);
        }

        return ret;
}

int setBuffersFormat(struct VideoInfo *cam_dev)
{
        int ret = 0;
		if ((cam_dev->preview.format.fmt.pix.width != 0) && (cam_dev->preview.format.fmt.pix.height != 0)) {
        int pixelformat = cam_dev->preview.format.fmt.pix.pixelformat;

        ret = ioctl(cam_dev->fd, VIDIOC_S_FMT, &cam_dev->preview.format);
        if (ret < 0) {
                DBG_LOGB("Open: VIDIOC_S_FMT Failed: %s, ret=%d\n", strerror(errno), ret);
        }

        CAMHAL_LOGIB("Width * Height %d x %d expect pixelfmt:%.4s, get:%.4s\n",
                        cam_dev->preview.format.fmt.pix.width,
                        cam_dev->preview.format.fmt.pix.height,
                        (char*)&pixelformat,
                        (char*)&cam_dev->preview.format.fmt.pix.pixelformat);
		}
        return ret;
}

int start_capturing(struct VideoInfo *vinfo)
{
        int ret = 0;
        int i;
        enum v4l2_buf_type type;
        struct  v4l2_buffer buf;

        if (vinfo->isStreaming) {
                DBG_LOGA("already stream on\n");
        }
        CLEAR(vinfo->preview.rb);

        vinfo->preview.rb.count = 6;
        vinfo->preview.rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        //TODO DMABUF & ION
        vinfo->preview.rb.memory = V4L2_MEMORY_MMAP;

        ret = ioctl(vinfo->fd, VIDIOC_REQBUFS, &vinfo->preview.rb);
        if (ret < 0) {
                DBG_LOGB("camera idx:%d does not support "
                                "memory mapping, errno=%d\n", vinfo->idx, errno);
        }

        if (vinfo->preview.rb.count < 2) {
                DBG_LOGB( "Insufficient buffer memory on /dev/video%d, errno=%d\n",
                                vinfo->idx, errno);
                return -EINVAL;
        }

        for (i = 0; i < (int)vinfo->preview.rb.count; ++i) {

                CLEAR(vinfo->preview.buf);

                vinfo->preview.buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                vinfo->preview.buf.memory      = V4L2_MEMORY_MMAP;
                vinfo->preview.buf.index       = i;

                if (-1 == ioctl(vinfo->fd, VIDIOC_QUERYBUF, &vinfo->preview.buf)){
                        DBG_LOGB("VIDIOC_QUERYBUF, errno=%d", errno);
                }

	        vinfo->mem[i] = mmap(NULL /* start anywhere */,
	                                vinfo->preview.buf.length,
                                        PROT_READ | PROT_WRITE /* required */,
                                        MAP_SHARED /* recommended */,
                                        vinfo->fd,
	                                vinfo->preview.buf.m.offset);

                if (MAP_FAILED == vinfo->mem[i]) {
                        DBG_LOGB("mmap failed, errno=%d\n", errno);
                }
        }
        ////////////////////////////////
        for (i = 0; i < (int)vinfo->preview.rb.count; ++i) {

                CLEAR(buf);
                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = V4L2_MEMORY_MMAP;
                buf.index = i;

                if (-1 == ioctl(vinfo->fd, VIDIOC_QBUF, &buf))
                        DBG_LOGB("VIDIOC_QBUF failed, errno=%d\n", errno);
        }

        type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (-1 == ioctl(vinfo->fd, VIDIOC_STREAMON, &type))
                DBG_LOGB("VIDIOC_STREAMON, errno=%d\n", errno);

		vinfo->isStreaming = true;
       
        return 0;
}

int stop_capturing(struct VideoInfo *vinfo)
{
        enum v4l2_buf_type type;
        int res = 0;
        int i;

        if (!vinfo->isStreaming)
                return -1;

        type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (-1 == ioctl(vinfo->fd, VIDIOC_STREAMOFF, &type)){
                DBG_LOGB("VIDIOC_STREAMOFF, errno=%d", errno);
                res = -1;
        }

        for (i = 0; i < (int)vinfo->preview.rb.count; ++i) {
                if (-1 == munmap(vinfo->mem[i], vinfo->preview.buf.length)) {
                        DBG_LOGB("munmap failed errno=%d", errno);
                        res = -1;
                }
        }
		
		vinfo->isStreaming = false;

		return res;
}

void *get_frame(struct VideoInfo *vinfo)
{
        CLEAR(vinfo->preview.buf);

        vinfo->preview.buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        vinfo->preview.buf.memory = V4L2_MEMORY_MMAP;

        if (-1 == ioctl(vinfo->fd, VIDIOC_DQBUF, &vinfo->preview.buf)) {
                switch (errno) {
                        case EAGAIN:
                                return NULL;

                        case EIO:
                                /* Could ignore EIO, see spec. */

                                /* fall through */

                        default:
                                DBG_LOGB("VIDIOC_DQBUF failed, errno=%d\n", errno);
                                exit(1);
                }
		DBG_LOGB("VIDIOC_DQBUF failed, errno=%d\n", errno);
        }
        //DBG_LOGA("get frame\n");

        return vinfo->mem[vinfo->preview.buf.index];
}

int putback_frame(struct VideoInfo *vinfo)
{

        if (-1 == ioctl(vinfo->fd, VIDIOC_QBUF, &vinfo->preview.buf))
                DBG_LOGB("QBUF failed error=%d\n", errno);

        return 0;
}

int start_picture(struct VideoInfo *vinfo, int rotate)
{
	    int ret = 0;
        int i;
        enum v4l2_buf_type type;
        struct  v4l2_buffer buf;

        CLEAR(vinfo->picture.rb);

		stop_capturing(vinfo);
		
		//step 1 : ioctl  VIDIOC_S_FMT
		ret = ioctl(vinfo->fd, VIDIOC_S_FMT, &vinfo->picture.format);
        if (ret < 0) {
                DBG_LOGB("Open: VIDIOC_S_FMT Failed: %s, ret=%d\n", strerror(errno), ret);
        }

		//step 2 : request buffer
        vinfo->picture.rb.count = 1;
        vinfo->picture.rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        //TODO DMABUF & ION
        vinfo->picture.rb.memory = V4L2_MEMORY_MMAP;

        ret = ioctl(vinfo->fd, VIDIOC_REQBUFS, &vinfo->picture.rb);
        if (ret < 0) {
                DBG_LOGB("camera idx:%d does not support "
                                "memory mapping, errno=%d\n", vinfo->idx, errno);
        }

        if (vinfo->picture.rb.count < 1) {
                DBG_LOGB( "Insufficient buffer memory on /dev/video%d, errno=%d\n",
                                vinfo->idx, errno);
                return -EINVAL;
        }

		//step 3: mmap buffer
        for (i = 0; i < (int)vinfo->picture.rb.count; ++i) {

                CLEAR(vinfo->picture.buf);

                vinfo->picture.buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                vinfo->picture.buf.memory      = V4L2_MEMORY_MMAP;
                vinfo->picture.buf.index       = i;

                if (-1 == ioctl(vinfo->fd, VIDIOC_QUERYBUF, &vinfo->picture.buf)){
                        DBG_LOGB("VIDIOC_QUERYBUF, errno=%d", errno);
                }
	        	vinfo->mem_pic[i] = mmap(NULL /* start anywhere */,
	                                vinfo->picture.buf.length,
                                        PROT_READ | PROT_WRITE /* required */,
                                        MAP_SHARED /* recommended */,
                                        vinfo->fd,
	                                vinfo->picture.buf.m.offset);

                if (MAP_FAILED == vinfo->mem_pic[i]) {
                        DBG_LOGB("mmap failed, errno=%d\n", errno);
                }
        }

		//step 4 : QBUF
		        ////////////////////////////////
        for (i = 0; i < (int)vinfo->picture.rb.count; ++i) {

                CLEAR(buf);
                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = V4L2_MEMORY_MMAP;
                buf.index = i;

                if (-1 == ioctl(vinfo->fd, VIDIOC_QBUF, &buf))
                        DBG_LOGB("VIDIOC_QBUF failed, errno=%d\n", errno);
        }

        if (vinfo->isPicture) {
                DBG_LOGA("already stream on\n");
        }

		set_rotate_value(vinfo->fd,rotate);
		//step 5: Stream ON
        type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (-1 == ioctl(vinfo->fd, VIDIOC_STREAMON, &type))
                DBG_LOGB("VIDIOC_STREAMON, errno=%d\n", errno);
        vinfo->isPicture = true;

	return 0;
	
}

void *get_picture(struct VideoInfo *vinfo)
{
        CLEAR(vinfo->picture.buf);
		
        vinfo->picture.buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        vinfo->picture.buf.memory = V4L2_MEMORY_MMAP;

        if (-1 == ioctl(vinfo->fd, VIDIOC_DQBUF, &vinfo->picture.buf)) {
			 switch (errno) {
             case EAGAIN:
             	return NULL;
        	 case EIO:
                /* Could ignore EIO, see spec. */
				/* fall through */
             default:
                DBG_LOGB("VIDIOC_DQBUF failed, errno=%d\n", errno);
                exit(1);
            }
        }
        DBG_LOGA("get picture\n");
        return vinfo->mem_pic[vinfo->picture.buf.index];
}

void stop_picture(struct VideoInfo *vinfo)
{
	    enum v4l2_buf_type type;
		struct  v4l2_buffer buf;
        int i;

        if (!vinfo->isPicture)
                return ;
		
		//QBUF
		for (i = 0; i < (int)vinfo->picture.rb.count; ++i) {
            CLEAR(buf);
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;
            if (-1 == ioctl(vinfo->fd, VIDIOC_QBUF, &buf))
                    DBG_LOGB("VIDIOC_QBUF failed, errno=%d\n", errno);
        }
		
		//stream off and unmap buffer
        type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (-1 == ioctl(vinfo->fd, VIDIOC_STREAMOFF, &type))
                DBG_LOGB("VIDIOC_STREAMOFF, errno=%d", errno);
		
        for (i = 0; i < (int)vinfo->picture.rb.count; i++)
        {
        	if (-1 == munmap(vinfo->mem_pic[i], vinfo->picture.buf.length))
            DBG_LOGB("munmap failed errno=%d", errno);
        }

		set_rotate_value(vinfo->fd,0);
		vinfo->isPicture = false;
		setBuffersFormat(vinfo);
		start_capturing(vinfo);
		
}

void camera_close(struct VideoInfo *vinfo)
{
    if (NULL == vinfo) {
        DBG_LOGA("vinfo is null\n");
        return ;
    }

        if (-1 == close(vinfo->fd))
                DBG_LOGB("close failed, errno=%d\n", errno);

        vinfo->fd = -1;
}
#ifdef __cplusplus
//}
#endif
#endif

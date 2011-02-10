
#ifndef AMLOGIC_HARDWARE_CAMERA_HARDWARE_H
#define AMLOGIC_HARDWARE_CAMERA_HARDWARE_H

#include <utils/threads.h>
#include <camera/CameraHardwareInterface.h>
#include <binder/MemoryBase.h>
#include <binder/MemoryHeapBase.h>
#include <utils/threads.h>

namespace android {

class CameraInterface
{
public:
	CameraInterface(){};
	virtual ~CameraInterface(){};
	virtual int  Open() = 0;
	virtual int  Close() = 0;
	virtual int  StartPreview() = 0;
	virtual int  StopPreview() = 0;
	virtual void InitParameters(CameraParameters& pParameters) = 0;
	virtual void SetParameters(CameraParameters& pParameters) = 0;
	virtual void GetPreviewFrame(uint8_t* framebuf) = 0;
	virtual void GetRawFrame(uint8_t* framebuf) = 0;
	virtual void GetJpegFrame(uint8_t* framebuf) = 0;
};


class AmlogicCameraHardware : public CameraHardwareInterface {
public:
    virtual sp<IMemoryHeap> getPreviewHeap() const;
    virtual sp<IMemoryHeap> getRawHeap() const;

    virtual void        setCallbacks(notify_callback notify_cb,
                                     data_callback data_cb,
                                     data_callback_timestamp data_cb_timestamp,
                                     void* user);

    virtual void        enableMsgType(int32_t msgType);
    virtual void        disableMsgType(int32_t msgType);
    virtual bool        msgTypeEnabled(int32_t msgType);

    virtual status_t    startPreview();
    virtual void        stopPreview();
    virtual bool        previewEnabled();

    virtual status_t    startRecording();
    virtual void        stopRecording();
    virtual bool        recordingEnabled();
    virtual void        releaseRecordingFrame(const sp<IMemory>& mem);

    virtual status_t    autoFocus();
    virtual status_t    cancelAutoFocus();
    virtual status_t    takePicture();
    virtual status_t    cancelPicture();
    virtual status_t    dump(int fd, const Vector<String16>& args) const;
    virtual status_t    setParameters(const CameraParameters& params);
    virtual CameraParameters  getParameters() const;
    virtual status_t    sendCommand(int32_t command, int32_t arg1,
                                    int32_t arg2);
    virtual void release();
    
    //virtual bool         useOverlay() {return true;}//pan

    static sp<CameraHardwareInterface> createInstance();

private:
                        AmlogicCameraHardware();
    virtual             ~AmlogicCameraHardware();

    static wp<CameraHardwareInterface> singleton;

    static const int kBufferCount = 4;

    class PreviewThread : public Thread {
        AmlogicCameraHardware* mHardware;
    public:
        PreviewThread(AmlogicCameraHardware* hw) :
#ifdef SINGLE_PROCESS
            // In single process mode this thread needs to be a java thread,
            // since we won't be calling through the binder.
            Thread(true),
#else
            Thread(false),
#endif
              mHardware(hw) { }
        virtual void onFirstRef() {
            run("CameraPreviewThread", PRIORITY_URGENT_DISPLAY);
        }
        virtual bool threadLoop() {
            mHardware->previewThread();
            // loop until we need to quit
            return true;
        }
    };

    void initDefaultParameters();
    void initHeapLocked();

    int previewThread();

    static int beginAutoFocusThread(void *cookie);
    int autoFocusThread();

    static int beginPictureThread(void *cookie);
    int pictureThread();

    mutable Mutex       mLock;

    CameraParameters    mParameters;

    sp<MemoryHeapBase>  mPreviewHeap;
    sp<MemoryHeapBase>  mRawHeap;
    sp<MemoryBase>      mBuffers[kBufferCount];

    //FakeCamera          *mFakeCamera;
    bool                mPreviewRunning;
    int                 mPreviewFrameSize;

    // protected by mLock
    sp<PreviewThread>   mPreviewThread;

    notify_callback    mNotifyCb;
    data_callback      mDataCb;
    data_callback_timestamp mDataCbTimestamp;
    void               *mCallbackCookie;

    int32_t             mMsgEnabled;

    // only used from PreviewThread
    int                 mCurrentPreviewFrame;

	CameraInterface* 	mCamera;
};

}; // namespace android



#endif




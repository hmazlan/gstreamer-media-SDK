#ifndef GST_MFX_TASK_H
#define GST_MFX_TASK_H

#include "gstmfxminiobject.h"
#include "gstmfxdisplay.h"

#include <mfxvideo.h>
#include <va/va.h>

G_BEGIN_DECLS

#define GST_MFX_TASK(obj) \
	((GstMfxTask *) (obj))

#define GST_MFX_TASK_SESSION(task) \
	gst_mfx_task_get_session(task)

#define GST_MFX_TASK_DISPLAY(task) \
	gst_mfx_task_get_display(task)


typedef struct _GstMfxTask GstMfxTask;
typedef struct _GstMfxContext GstMfxContext;

typedef enum {
	GST_MFX_TASK_INVALID = 0,
	GST_MFX_TASK_DECODER = (1 << 0),
	GST_MFX_TASK_VPP_IN = (1 << 1),
	GST_MFX_TASK_VPP_OUT = (1 << 2),
	GST_MFX_TASK_ENCODER = (1 << 3),
} GstMfxTaskType;

GstMfxTask *
gst_mfx_task_new (GstMfxContext * context,
	guint type_flags);

GstMfxTask *
gst_mfx_task_new_with_session (GstMfxContext * context,
	mfxSession * session, guint type_flags);

GstMfxTask *
gst_mfx_task_ref (GstMfxTask * task);

void
gst_mfx_task_unref (GstMfxTask * task);

void
gst_mfx_task_replace (GstMfxTask ** old_task_ptr,
	GstMfxTask * new_task);

gboolean
gst_mfx_task_set_type_flags (GstMfxTask * task, guint flags);

gboolean
gst_mfx_task_has_type (GstMfxTask * task, guint flags);

guint
gst_mfx_task_get_type_flags (GstMfxTask * alloc);

GstMfxDisplay *
gst_mfx_task_get_display(GstMfxTask * task);

GAsyncQueue *
gst_mfx_task_get_surfaces (GstMfxTask * task);

mfxSession
gst_mfx_task_get_session (GstMfxTask * task);

mfxFrameInfo *
gst_mfx_task_get_frame_info (GstMfxTask * task);

mfxStatus
gst_mfx_task_frame_alloc (mfxHDL pthis, mfxFrameAllocRequest *req,
	mfxFrameAllocResponse *resp);

mfxStatus
gst_mfx_task_frame_free (mfxHDL pthis, mfxFrameAllocResponse *resp);

mfxStatus
gst_mfx_task_frame_lock (mfxHDL pthis, mfxMemId mid, mfxFrameData *ptr);

mfxStatus
gst_mfx_task_frame_unlock (mfxHDL pthis, mfxMemId mid, mfxFrameData *ptr);

mfxStatus
gst_mfx_task_frame_get_hdl (mfxHDL pthis, mfxMemId mid, mfxHDL *hdl);

G_END_DECLS

#endif /* GST_MFX_TASK_H */

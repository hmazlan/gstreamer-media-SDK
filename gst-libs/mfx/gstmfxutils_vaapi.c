#include <va/va.h>
#include "sysdeps.h"
#include "gstmfxutils_vaapi.h"
#include "gstmfxminiobject.h"

#define DEBUG 1
#include "gstmfxdebug.h"

struct _VaapiImage {
    /*< private >*/
    GstMfxMiniObject    parent_instance;
    GstMfxDisplay       *display;
    GstVideoFormat      internal_format;
    guchar              *image_data;
    guint               width;
    guint               height;
    VAImage             image;
};

static gboolean
_vaapi_image_map(VaapiImage *image);

static gboolean
_vaapi_image_unmap(VaapiImage *image);

static gboolean
_vaapi_image_set_image(VaapiImage *image, const VAImage *va_image);

static void
vaapi_image_finalize(VaapiImage *image)
{
	VAImageID image_id;
	VAStatus status;

    _vaapi_image_unmap(image);

	image_id = vaapi_image_get_id(image);
	GST_DEBUG("image %" GST_MFX_ID_FORMAT, GST_MFX_ID_ARGS(image_id));

    if (image_id != VA_INVALID_ID) {
		GST_MFX_DISPLAY_LOCK(image->display);
		status = vaDestroyImage(GST_MFX_DISPLAY_VADISPLAY(image->display), image_id);
		GST_MFX_DISPLAY_UNLOCK(image->display);
        if (!vaapi_check_status(status, "vaDestroyImage()"))
			g_warning("failed to destroy image %" GST_MFX_ID_FORMAT,
				GST_MFX_ID_ARGS(image_id));
    }
    gst_mfx_display_unref(image->display);
}

static gboolean
vaapi_image_create(VaapiImage *image,
    guint width, guint height)
{
    VAStatus status;
    VAImageFormat va_format = {
        .fourcc         = VA_FOURCC_NV12,
        .byte_order     = VA_LSB_FIRST,
        .bits_per_pixel = 8,
        .depth          = 8,
    };

	GST_MFX_DISPLAY_LOCK(image->display);
    status = vaCreateImage(
		GST_MFX_DISPLAY_VADISPLAY(image->display),
        &va_format,
        width,
        height,
        &image->image
    );
	GST_MFX_DISPLAY_UNLOCK(image->display);

    if (status != VA_STATUS_SUCCESS)
        return FALSE;

    image->internal_format = GST_VIDEO_FORMAT_NV12;
    image->width = width;
    image->height = height;

    return TRUE;
}

static inline const GstMfxMiniObjectClass *
vaapi_image_class(void)
{
    static const GstMfxMiniObjectClass \
        VaapiImageClass = {
            sizeof (VaapiImage),
            (GDestroyNotify)vaapi_image_finalize
        };
    return &VaapiImageClass;
}

/**
 * vaapi_image_new:
 * @display: a #GstVaapiDisplay
 * @format: a #GstVideoFormat
 * @width: the requested image width
 * @height: the requested image height
 *
 * Creates a new #VaapiImage with the specified format and
 * dimensions.
 *
 * Return value: the newly allocated #VaapiImage object
 */
VaapiImage *
vaapi_image_new(
	GstMfxDisplay	*display,
    guint           width,
    guint           height
)
{
    VaapiImage *image;

    g_return_val_if_fail(width > 0, NULL);
    g_return_val_if_fail(height > 0, NULL);
    g_return_val_if_fail(display != NULL, NULL);

    image = (VaapiImage *)
        gst_mfx_mini_object_new0(vaapi_image_class());

    if(!image)
        return NULL;

    image->display = gst_mfx_display_ref(display);
    image->image.image_id = VA_INVALID_ID;
    image->image.buf = VA_INVALID_ID;
    if (!vaapi_image_create(image, width, height))
        goto error;
    return image;

error:
    vaapi_image_unref(image);
    return NULL;
}

/**
 * vaapi_image_new_with_image:
 * @display: a #GstVaapiDisplay
 * @va_image: a VA image
 *
 * Creates a new #VaapiImage from a foreign VA image. The image
 * format and dimensions will be extracted from @va_image. This
 * function is mainly used by gst_mfx_surface_derive_image() to bind
 * a VA image to a #VaapiImage object.
 *
 * Return value: the newly allocated #VaapiImage object
 */
VaapiImage *
vaapi_image_new_with_image(GstMfxDisplay *display, VAImage *va_image)
{
    VaapiImage *image;

    g_return_val_if_fail(va_image, NULL);
    g_return_val_if_fail(va_image->image_id != VA_INVALID_ID, NULL);
    g_return_val_if_fail(va_image->buf != VA_INVALID_ID, NULL);

	image = (VaapiImage *)
        gst_mfx_mini_object_new0(vaapi_image_class());

    if(!image)
        return NULL;

    image->display = gst_mfx_display_ref(display);
    if (!_vaapi_image_set_image(image, va_image))
        goto error;
    return image;

error:
    vaapi_image_unref(image);
    return NULL;
}

/**
 * vaapi_image_get_id:
 * @image: a #VaapiImage
 *
 * Returns the underlying VAImageID of the @image.
 *
 * Return value: the underlying VA image id
 */
VAImageID
vaapi_image_get_id(VaapiImage *image)
{
    VAImage *va_image;
    g_return_val_if_fail(image != NULL, VA_INVALID_ID);
    va_image = &image->image;

	return va_image->image_id;
}

/**
 * vaapi_image_get_image:
 * @image: a #VaapiImage
 * @va_image: a VA image
 *
 * Fills @va_image with the VA image used internally.
 *
 * Return value: %TRUE on success
 */
gboolean
vaapi_image_get_image(VaapiImage *image, VAImage *va_image)
{
    g_return_val_if_fail(image != NULL, FALSE);

    if (va_image)
        *va_image = image->image;

    return TRUE;
}

/*
 * _vaapi_image_set_image:
 * @image: a #VaapiImage
 * @va_image: a VA image
 *
 * Initializes #VaapiImage with a foreign VA image. This function
 * will try to "linearize" the VA image. i.e. making sure that the VA
 * image offsets into the data buffer are in increasing order with the
 * number of planes available in the image.
 *
 * This is an internal function used by vaapi_image_new_with_image().
 *
 * Return value: %TRUE on success
 */
gboolean
_vaapi_image_set_image(VaapiImage *image, const VAImage *va_image)
{
    image->internal_format = GST_VIDEO_FORMAT_NV12;
    image->image           = *va_image;
    image->width           = va_image->width;
    image->height          = va_image->height;

    return TRUE;
}

/**
 * vaapi_image_get_format:
 * @image: a #VaapiImage
 *
 * Returns the #GstVideoFormat the @image was created with.
 *
 * Return value: the #GstVideoFormat
 */
GstVideoFormat
vaapi_image_get_format(VaapiImage *image)
{
    g_return_val_if_fail(image != NULL, 0);

    return image->internal_format;
}

/**
 * vaapi_image_get_width:
 * @image: a #VaapiImage
 *
 * Returns the @image width.
 *
 * Return value: the image width, in pixels
 */
guint
vaapi_image_get_width(VaapiImage *image)
{
    g_return_val_if_fail(image != NULL, 0);

    return image->width;
}

/**
 * vaapi_image_get_height:
 * @image: a #VaapiImage
 *
 * Returns the @image height.
 *
 * Return value: the image height, in pixels.
 */
guint
vaapi_image_get_height(VaapiImage *image)
{
    g_return_val_if_fail(image != NULL, 0);

    return image->height;
}

/**
 * vaapi_image_get_size:
 * @image: a #VaapiImage
 * @pwidth: return location for the width, or %NULL
 * @pheight: return location for the height, or %NULL
 *
 * Retrieves the dimensions of a #VaapiImage.
 */
void
vaapi_image_get_size(VaapiImage *image, guint *pwidth, guint *pheight)
{
    g_return_if_fail(image != NULL);

    if (pwidth)
        *pwidth = image->width;

    if (pheight)
        *pheight = image->height;
}

/**
 * vaapi_image_is_mapped:
 * @image: a #VaapiImage
 *
 * Checks whether the @image is currently mapped or not.
 *
 * Return value: %TRUE if the @image is mapped
 */
static inline gboolean
_vaapi_image_is_mapped(VaapiImage *image)
{
    return image->image_data != NULL;
}

gboolean
vaapi_image_is_mapped(VaapiImage *image)
{
    g_return_val_if_fail(image != NULL, FALSE);

    return _vaapi_image_is_mapped(image);
}

/**
 * vaapi_image_map:
 * @image: a #VaapiImage
 *
 * Maps the image data buffer. The actual pixels are returned by the
 * vaapi_image_get_plane() function.
 *
 * Return value: %TRUE on success
 */
gboolean
vaapi_image_map(VaapiImage *image)
{
    g_return_val_if_fail(image != NULL, FALSE);

    return _vaapi_image_map(image);
}

gboolean
_vaapi_image_map(VaapiImage *image)
{
	VAStatus status;

    if (_vaapi_image_is_mapped(image))
        goto map_success;

	GST_MFX_DISPLAY_LOCK(image->display);
    status = vaMapBuffer(
		GST_MFX_DISPLAY_VADISPLAY(image->display),
        image->image.buf,
        (void **)&image->image_data
    );
	GST_MFX_DISPLAY_UNLOCK(image->display);
    if (!vaapi_check_status(status, "vaMapBuffer()"))
        return FALSE;

map_success:
    return TRUE;
}

/**
 * vaapi_image_unmap:
 * @image: a #VaapiImage
 *
 * Unmaps the image data buffer. Pointers to pixels returned by
 * vaapi_image_get_plane() are then no longer valid.
 *
 * Return value: %TRUE on success
 */
gboolean
vaapi_image_unmap(VaapiImage *image)
{
    g_return_val_if_fail(image != NULL, FALSE);

    return _vaapi_image_unmap(image);
}

gboolean
_vaapi_image_unmap(VaapiImage *image)
{
	VAStatus status;

    if (!_vaapi_image_is_mapped(image))
        return TRUE;

	GST_MFX_DISPLAY_LOCK(image->display);
    status = vaUnmapBuffer(
		GST_MFX_DISPLAY_VADISPLAY(image->display),
        image->image.buf
    );
	GST_MFX_DISPLAY_UNLOCK(image->display);
    if (!vaapi_check_status(status, "vaUnmapBuffer()"))
        return FALSE;

    image->image_data = NULL;
    return TRUE;
}

/**
 * vaapi_image_get_plane_count:
 * @image: a #VaapiImage
 *
 * Retrieves the number of planes available in the @image. The @image
 * must be mapped for this function to work properly.
 *
 * Return value: the number of planes available in the @image
 */
guint
vaapi_image_get_plane_count(VaapiImage *image)
{
    g_return_val_if_fail(image != NULL, 0);

    return image->image.num_planes;
}

/**
 * vaapi_image_get_plane:
 * @image: a #VaapiImage
 * @plane: the requested plane number
 *
 * Retrieves the pixels data to the specified @plane. The @image must
 * be mapped for this function to work properly.
 *
 * Return value: the pixels data of the specified @plane
 */
guchar *
vaapi_image_get_plane(VaapiImage *image, guint plane)
{
    g_return_val_if_fail(image != NULL, NULL);
    g_return_val_if_fail(_vaapi_image_is_mapped(image), NULL);
    g_return_val_if_fail(plane < image->image.num_planes, NULL);

    return image->image_data + image->image.offsets[plane];
}

/**
 * vaapi_image_get_pitch:
 * @image: a #VaapiImage
 * @plane: the requested plane number
 *
 * Retrieves the line size (stride) of the specified @plane. The
 * @image must be mapped for this function to work properly.
 *
 * Return value: the line size (stride) of the specified plane
 */
guint
vaapi_image_get_pitch(VaapiImage *image, guint plane)
{
    g_return_val_if_fail(image != NULL, 0);
    g_return_val_if_fail(plane < image->image.num_planes, 0);

    return image->image.pitches[plane];
}

/**
 * vaapi_image_get_data_size:
 * @image: a #VaapiImage
 *
 * Retrieves the underlying image data size. This function could be
 * used to determine whether the image has a compatible layout with
 * another image structure.
 *
 * Return value: the whole image data size of the @image
 */
guint
vaapi_image_get_data_size(VaapiImage *image)
{
    g_return_val_if_fail(image != NULL, 0);

    return image->image.data_size;
}

/**
 * vaapi_image_get_offset
 * @image: a #VaapiImage
 *
 * Retrives the offsets of the each plane
 *
 * Return value: the offset value of the plane of the image
 */
guint
vaapi_image_get_offset(VaapiImage *image, guint plane)
{
    g_return_val_if_fail(image != NULL, 0);
    g_return_val_if_fail(plane < image->image.num_planes, 0);

    return image->image.offsets[plane];
}

/**
 * vaapi_image_ref
 * @image: a #VaapiImage
 *
 * Atomically increases the reference count of the given @image by one.
 *
 * Returns: The same @image argument
 */
VaapiImage *
vaapi_image_ref(VaapiImage * image)
{
    g_return_val_if_fail(image != NULL, NULL);

    return gst_mfx_mini_object_ref(GST_MFX_MINI_OBJECT (image));
}

/**
 * vaapi_image_unref
 * @image: a #VaapiImage
 *
 * Atomically decreases the reference count of the given @image by one.
 * If the reference count reaches zero, the object will be free'd.
 */
void
vaapi_image_unref(VaapiImage * image)
{
    gst_mfx_mini_object_unref(GST_MFX_MINI_OBJECT (image));
}

/**
 * vaapi_image_replace
 * @old_image_ptr: a pointer to a #VaapiImage
 * @new_image: a #VaapiImage
 *
 * Atomically replaces the image object held in @old_image_ptr with
 * @new_image. This means the @old_image_ptr shall reference a valid
 * object. However, @new_image can be NULL.
 */
void
vaapi_image_replace(VaapiImage ** old_image_ptr,
        VaapiImage * new_image)
{
    g_return_if_fail(old_image_ptr != NULL);

    gst_mfx_mini_object_replace((GstMfxMiniObject **)old_image_ptr,
            GST_MFX_MINI_OBJECT(new_image));
}

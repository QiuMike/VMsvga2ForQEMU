/*
 *  VMsvga22DContext.cpp
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on August 10th 2009.
 *  Copyright 2009-2011 Zenith432. All rights reserved.
 *  Portions Copyright (c) Apple Computer, Inc.
 *
 *  Permission is hereby granted, free of charge, to any person
 *  obtaining a copy of this software and associated documentation
 *  files (the "Software"), to deal in the Software without
 *  restriction, including without limitation the rights to use, copy,
 *  modify, merge, publish, distribute, sublicense, and/or sell copies
 *  of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be
 *  included in all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 *  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 *  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 *  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

#include <IOKit/IOLib.h>
#include <IOKit/graphics/IOGraphicsInterfaceTypes.h>
#include "VLog.h"
#include "VMsvga2Accel.h"
#include "VMsvga2Surface.h"
#include "VMsvga22DContext.h"
#include "UCMethods.h"

#include "svga_apple_header.h"
#include "svga_overlay.h"
#include "svga_apple_footer.h"

#define CLASS VMsvga22DContext
#define super IOUserClient
OSDefineMetaClassAndStructors(VMsvga22DContext, IOUserClient);

#if LOGGING_LEVEL >= 1
#define TDLog(log_level, ...) do { if (log_level <= m_log_level) VLog("IO2D: ", ##__VA_ARGS__); } while (false)
#else
#define TDLog(log_level, ...)
#endif

#define HIDDEN __attribute__((visibility("hidden")))

static
IOExternalMethod const iofbFuncsCache[kIOVM2DNumMethods] =
{
// Note: methods from IONV2DContext
{0, reinterpret_cast<IOMethod>(&CLASS::set_surface), kIOUCScalarIStructO, 2, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::get_config), kIOUCScalarIScalarO, 0, 2},
{0, reinterpret_cast<IOMethod>(&CLASS::get_surface_info1), kIOUCScalarIStructO, 2, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::swap_surface), kIOUCScalarIScalarO, 1, 1},
{0, reinterpret_cast<IOMethod>(&CLASS::scale_surface), kIOUCScalarIScalarO, 3, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::lock_memory), kIOUCScalarIStructO, 1, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::unlock_memory), kIOUCScalarIScalarO, 1, 1},
{0, reinterpret_cast<IOMethod>(&CLASS::finish), kIOUCScalarIScalarO, 1, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::declare_image), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::create_image), kIOUCScalarIStructO, 2, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::create_transfer), kIOUCScalarIStructO, 2, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::delete_image), kIOUCScalarIScalarO, 1, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::wait_image), kIOUCScalarIScalarO, 1, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::set_surface_paging_options), kIOUCStructIStructO, 12, 12},
{0, reinterpret_cast<IOMethod>(&CLASS::set_surface_vsync_options), kIOUCStructIStructO, 12, 12},
{0, reinterpret_cast<IOMethod>(&CLASS::set_macrovision), kIOUCScalarIScalarO, 1, 0},
// Note: Methods from NV2DContext
{0, reinterpret_cast<IOMethod>(&CLASS::read_configs), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::read_config_Ex), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::get_surface_info2), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::kernel_printf), kIOUCScalarIStructI, 0, kIOUCVariableStructureSize},
// Note: VM Methods
{0, reinterpret_cast<IOMethod>(&CLASS::CopyRegion), kIOUCScalarIStructI, 3, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::useAccelUpdates), kIOUCScalarIScalarO, 1, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::RectCopy), kIOUCScalarIStructI, 0, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::RectFill), kIOUCScalarIStructI, 1, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&VMsvga2Accel::UpdateFramebufferAutoRing), kIOUCScalarIStructI, 0, 4U * sizeof(UInt32)}
};

#pragma mark -
#pragma mark IOUserClient Methods
#pragma mark -

IOExternalMethod* CLASS::getTargetAndMethodForIndex(IOService** targetP, UInt32 index)
{
	if (!targetP || index >= kIOVM2DNumMethods)
		return 0;
	switch (index) {
		case kIOVM2DUpdateFramebuffer:
			if (m_provider)
				*targetP = m_provider;
			else
				return 0;
			break;
		default:
			*targetP = this;
			break;
	}
	return const_cast<IOExternalMethod*>(&iofbFuncsCache[index]);
}

IOReturn CLASS::clientClose()
{
	TDLog(2, "%s\n", __FUNCTION__);
	if (m_surface_client) {
		m_surface_client->release();
		m_surface_client = 0;
	}
	m_framebufferIndex = 0;
	bTargetIsCGSSurface = false;
	if (m_provider)
		m_provider->useAccelUpdates(0, m_owning_task);
	if (!terminate(0))
		IOLog("%s: terminate failed\n", __FUNCTION__);
	m_owning_task = 0;
	m_provider = 0;
	return kIOReturnSuccess;
}

#if 0
IOReturn CLASS::clientMemoryForType(UInt32 type, IOOptionBits* options, IOMemoryDescriptor** memory)
{
	TDLog(2, "%s(%u, options_out, memory_out)\n", __FUNCTION__, static_cast<unsigned>(type));
	return super::clientMemoryForType(type, options, memory);
}
#endif

bool CLASS::start(IOService* provider)
{
	m_provider = OSDynamicCast(VMsvga2Accel, provider);
	if (!m_provider)
		return false;
	m_log_level = imax(m_provider->getLogLevelGA(), m_provider->getLogLevelAC());
	return super::start(provider);
}

bool CLASS::initWithTask(task_t owningTask, void* securityToken, UInt32 type)
{
	m_log_level = LOGGING_LEVEL;
	if (!super::initWithTask(owningTask, securityToken, type))
		return false;
	m_owning_task = owningTask;
	return true;
}

HIDDEN
CLASS* CLASS::withTask(task_t owningTask, void* securityToken, uint32_t type)
{
	CLASS* inst;

	inst = new CLASS;

	if (inst && !inst->initWithTask(owningTask, securityToken, type))
	{
		inst->release();
		inst = 0;
	}

	return (inst);
}

#pragma mark -
#pragma mark Private Methods
#pragma mark -

HIDDEN
IOReturn CLASS::locateSurface(uint32_t surface_id)
{
	if (!m_provider)
		return kIOReturnNotReady;
	m_surface_client = m_provider->findSurfaceForID(surface_id);
	if (!m_surface_client)
		return kIOReturnNotFound;
	m_surface_client->retain();
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark GA Support Methods
#pragma mark -

HIDDEN
IOReturn CLASS::useAccelUpdates(uintptr_t state)
{
	if (!m_provider)
		return kIOReturnNotReady;
	return m_provider->useAccelUpdates(state != 0, m_owning_task);
}

HIDDEN
IOReturn CLASS::RectCopy(struct IOBlitCopyRectangleStruct const* copyRects,
						 size_t copyRectsSize)
{
	if (bTargetIsCGSSurface) {
		TDLog(1, "%s: called with surface destination - unsupported\n", __FUNCTION__);
		return kIOReturnUnsupported;
	}
	if (!m_provider)
		return kIOReturnNotReady;
	return m_provider->RectCopy(m_framebufferIndex, copyRects, copyRectsSize);
}

HIDDEN
IOReturn CLASS::RectFill(uintptr_t color,
						 struct IOBlitRectangleStruct const* rects,
						 size_t rectsSize)
{
	if (bTargetIsCGSSurface) {
		TDLog(1, "%s: called with surface destination - unsupported\n", __FUNCTION__);
		return kIOReturnUnsupported;
	}
	if (!m_provider)
		return kIOReturnNotReady;
	return m_provider->RectFill(m_framebufferIndex, static_cast<uint32_t>(color), rects, rectsSize);
}

HIDDEN
IOReturn CLASS::CopyRegion(uintptr_t source_surface_id,
						   intptr_t destX,
						   intptr_t destY,
						   IOAccelDeviceRegion const* region,
						   size_t regionSize)
{
	IOReturn rc;
	uint32_t _source_surface_id;
	int _destX, _destY;
	VMsvga2Surface* source_surface;

	if (!region || regionSize < IOACCEL_SIZEOF_DEVICE_REGION(region))
		return kIOReturnBadArgument;

	if (!m_provider)
		return kIOReturnNotReady;

	if (bTargetIsCGSSurface && !m_surface_client)
		return kIOReturnNotReady;

	/*
	 * Correct for truncation done by IOUserClient dispatcher
	 */
	_source_surface_id = static_cast<uint32_t>(source_surface_id);
	_destX = static_cast<int>(destX);
	_destY = static_cast<int>(destY);

	if (_source_surface_id) {
		source_surface = m_provider->findSurfaceForID(_source_surface_id);
		if (!source_surface) {
			TDLog(1, "%s: Source Surface(%#x) not found\n", __FUNCTION__, _source_surface_id);
			return kIOReturnNotFound;
		}
		source_surface->retain();
		if (bTargetIsCGSSurface)
			rc = m_surface_client->copy_surface_region_to_self(source_surface,
															   _destX,
															   _destY,
															   region,
															   regionSize);
		else
			rc = source_surface->copy_self_region_to_framebuffer(m_framebufferIndex,
																 _destX,
																 _destY,
																 region,
																 regionSize);
		source_surface->release();
		return rc;
	}
	if (!bTargetIsCGSSurface) {
		/*
		 * destination is framebuffer, use classic mode
		 */
		if (!m_provider)
			return kIOReturnNotReady;
		return m_provider->CopyRegion(m_framebufferIndex, _destX, _destY, region, regionSize);
	}
	/*
	 * destination is a surface
	 */
	return m_surface_client->copy_framebuffer_region_to_self(m_framebufferIndex,
															 _destX,
															 _destY,
															 region,
															 regionSize);
}

#pragma mark -
#pragma mark IONV2DContext Methods
#pragma mark -

HIDDEN
IOReturn CLASS::set_surface(uintptr_t surface_id,
							eIOContextModeBits options,
							void* struct_out,
							size_t* struct_out_size)
{
	uint32_t vmware_pixel_format, apple_pixel_format;
	IOReturn rc;

	if (!struct_out || !struct_out_size)
		return kIOReturnBadArgument;
	bzero(struct_out, *struct_out_size);
	if (m_surface_client) {
		m_surface_client->release();
		m_surface_client = 0;
	}
	/*
	 * options == 0x800 -- has surface id
	 *            0x400 -- UYVY format ('2vuy' for Apple)
	 *			  0x200 -- YUY2 format ('yuvs' for Apple)
	 *			  0x100 -- BGRA format
	 *            0 -- framebuffer, surface_id is framebufferIndex
	 */
	if (!(options & 0x800U)) {
		/*
		 * set target to framebuffer
		 */
		bTargetIsCGSSurface = false;
		m_framebufferIndex = static_cast<uint32_t>(surface_id);
		return kIOReturnSuccess;
	}
	bTargetIsCGSSurface = true;
	/*
	 * Note: VMWARE_FOURCC_YV12 is not supported (planar 4:2:0 format)
	 */
	if (options & 0x400U) {
		vmware_pixel_format = VMWARE_FOURCC_UYVY;
		apple_pixel_format = kIO2vuyPixelFormat;
	} else if (options & 0x200U) {
		vmware_pixel_format = VMWARE_FOURCC_YUY2;
		apple_pixel_format = kIOYUVSPixelFormat;
	} else if (options & 0x100U) {
		vmware_pixel_format = 0;
		apple_pixel_format = kIO32BGRAPixelFormat;
	} else {
		vmware_pixel_format = 0;
		apple_pixel_format = 0;
	}
	rc = locateSurface(static_cast<uint32_t>(surface_id));
	if (rc != kIOReturnSuccess)
		return rc;
	return m_surface_client->context_set_surface(vmware_pixel_format, apple_pixel_format);
}

HIDDEN
IOReturn CLASS::get_config(uint32_t* config_1, uint32_t* config_2)
{
	if (!config_1 || !config_2)
		return kIOReturnBadArgument;
	*config_1 = 0U;
	/*
	 * TBD: transfer m_provider->getOptionsGA() as well
	 */
	if (m_provider)
		*config_2 = static_cast<uint32_t>(m_provider->getLogLevelGA());
	else
		*config_2 = 0U;
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::get_surface_info1(uintptr_t c1, eIOContextModeBits c2, void* struct_out, size_t* struct_out_size)
{
	TDLog(2, "%s(%lu, %lu, struct_out, %lu)\n", __FUNCTION__, c1, c2, *struct_out_size);
	return kIOReturnUnsupported;
}

HIDDEN
IOReturn CLASS::swap_surface(uintptr_t options, uint32_t* swapFlags)
{
	if (!bTargetIsCGSSurface) {
		TDLog(2, "%s: called with non-surface destination - unsupported\n", __FUNCTION__);
		return kIOReturnUnsupported;
	}
	if (!m_surface_client)
		return kIOReturnNotReady;
	m_surface_client->surface_flush_video(swapFlags);
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::scale_surface(uintptr_t options, uintptr_t width, uintptr_t height)
{
	if (!bTargetIsCGSSurface) {
		TDLog(2, "%s: called with non-surface destination - unsupported\n", __FUNCTION__);
		return kIOReturnUnsupported;
	}
	if (!m_surface_client)
		return kIOReturnNotReady;
	return m_surface_client->context_scale_surface(static_cast<IOOptionBits>(options),
												   static_cast<uint32_t>(width),
												   static_cast<uint32_t>(height));
}

HIDDEN
IOReturn CLASS::lock_memory(uintptr_t options, uint64_t* struct_out, size_t* struct_out_size)
{
	if (!struct_out || !struct_out_size || *struct_out_size < 2U * sizeof *struct_out)
		return kIOReturnBadArgument;
	if (!bTargetIsCGSSurface) {
		TDLog(2, "%s: called with non-surface destination - unsupported\n", __FUNCTION__);
		return kIOReturnUnsupported;
	}
	if (!m_surface_client)
		return kIOReturnNotReady;
	return m_surface_client->context_lock_memory(m_owning_task, &struct_out[0], &struct_out[1]);
}

HIDDEN
IOReturn CLASS::unlock_memory(uintptr_t options, uint32_t* swapFlags)
{
	if (!bTargetIsCGSSurface) {
		TDLog(2, "%s: called with non-surface destination - unsupported\n", __FUNCTION__);
		return kIOReturnUnsupported;
	}
	if (!m_surface_client)
		return kIOReturnNotReady;
	return m_surface_client->context_unlock_memory(swapFlags);
}

HIDDEN
IOReturn CLASS::finish(uintptr_t options)
{
#if 0
	if (!(options & (kIOBlitWaitAll2D | kIOBlitWaitAll)))
		return kIOReturnUnsupported;
#endif
	if (m_provider)
		return m_provider->SyncFIFO();
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::declare_image(UInt64 const* struct_in,
							  UInt64* struct_out,
							  size_t struct_in_size,
							  size_t* struct_out_size)
{
	TDLog(2, "%s(struct_in, struct_out, %lu, %lu)\n", __FUNCTION__, struct_in_size, *struct_out_size);
	return kIOReturnUnsupported;
}

HIDDEN
IOReturn CLASS::create_image(uintptr_t c1, uintptr_t c2, UInt64* struct_out, size_t* struct_out_size)
{
	TDLog(2, "%s(%lu, %lu, struct_out, %lu)\n", __FUNCTION__, c1, c2, *struct_out_size);
	return kIOReturnUnsupported;
}

HIDDEN
IOReturn CLASS::create_transfer(uintptr_t c1, uintptr_t c2, UInt64* struct_out, size_t* struct_out_size)
{
	TDLog(2, "%s(%lu, %lu, struct_out, %lu)\n", __FUNCTION__, c1, c2, *struct_out_size);
	return kIOReturnUnsupported;
}

HIDDEN
IOReturn CLASS::delete_image(uintptr_t image_id)
{
	TDLog(2, "%s(%lu)\n", __FUNCTION__, image_id);
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::wait_image(uintptr_t image_id)
{
	TDLog(2, "%s(%lu)\n", __FUNCTION__, image_id);
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::set_surface_paging_options(IOSurfacePagingControlInfoStruct const* struct_in,
										   IOSurfacePagingControlInfoStruct* struct_out,
										   size_t struct_in_size,
										   size_t* struct_out_size)
{
	TDLog(2, "%s(struct_in, struct_out, %lu, %lu)\n", __FUNCTION__, struct_in_size, *struct_out_size);
	return kIOReturnUnsupported;
}

HIDDEN
IOReturn CLASS::set_surface_vsync_options(IOSurfaceVsyncControlInfoStruct const* struct_in,
										  IOSurfaceVsyncControlInfoStruct* struct_out,
										  size_t struct_in_size,
										  size_t* struct_out_size)
{
	TDLog(2, "%s(struct_in, struct_out, %lu, %lu)\n", __FUNCTION__, struct_in_size, *struct_out_size);
	return kIOReturnUnsupported;
}

HIDDEN
IOReturn CLASS::set_macrovision(uintptr_t new_state)
{
	TDLog(3, "%s(%lu)\n", __FUNCTION__, new_state);
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark NV2DContext Methods
#pragma mark -

HIDDEN
IOReturn CLASS::read_configs(uint32_t const* struct_in,
							 uint32_t* struct_out,
							 size_t struct_in_size,
							 size_t* struct_out_size)
{
	TDLog(3, "%s(struct_in, struct_out, %lu, %lu)\n", __FUNCTION__, struct_in_size, *struct_out_size);
	if (!struct_in || !struct_out || !struct_out_size)
		return kIOReturnBadArgument;
	if (struct_in_size < sizeof(uint32_t) || *struct_out_size < sizeof(uint32_t))
		return kIOReturnBadArgument;
	if (*struct_in == 2U)
		*struct_out = 2U;
	else
		*struct_out = 0U;
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::read_config_Ex(uint32_t const* struct_in,
							   uint32_t* struct_out,
							   size_t struct_in_size,
							   size_t* struct_out_size)
{
	TDLog(3, "%s(struct_in, struct_out, %lu, %lu)\n", __FUNCTION__, struct_in_size, *struct_out_size);
	if (!struct_in || !struct_out || !struct_out_size)
		return kIOReturnBadArgument;
	if (struct_in_size < 2U * sizeof(uint32_t))
		return kIOReturnBadArgument;
	bzero(struct_out, *struct_out_size);
	switch (struct_in[0]) {
		case 144:
			/*
			 * GetBeamPosition
			 */
			if (*struct_out_size < 2U * sizeof(uint32_t))
				return kIOReturnBadArgument;
			struct_out[1] = 0;
			break;
		case 288:
			/*
			 * SetSurface
			 */
			if (*struct_out_size < 3U * sizeof(uint32_t))
				return kIOReturnBadArgument;
			struct_out[0] = 64;
			struct_out[2] = 16;
			break;
		default:
			return kIOReturnUnsupported;
	}
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::get_surface_info2(uint32_t const* struct_in,
								  uint32_t* struct_out,
								  size_t struct_in_size,
								  size_t* struct_out_size)
{
	TDLog(2, "%s(struct_in, struct_out, %lu, %lu)\n", __FUNCTION__, struct_in_size, *struct_out_size);
	return kIOReturnUnsupported;
}

HIDDEN
IOReturn CLASS::kernel_printf(char const* str, size_t str_size)
{
	TDLog(2, "%s: %.80s\n", __FUNCTION__, str);
	return kIOReturnSuccess;
}

//
// MPF Platform
// Resource Manager
// ���ߣ�SunnyCase
// ����ʱ�䣺2016-07-21
//
#include "stdafx.h"
#include "ResourceManagerBase.h"
#include "ResourceRef.h"
#include "RenderCommandBuffer.h"
using namespace WRL;
using namespace NS_PLATFORM;

#define CTOR_IMPL1(T) \
_container##T(std::make_shared<ResourceContainer<T>>())

ResourceManagerBase::ResourceManagerBase()
	:CTOR_IMPL1(LineGeometry), CTOR_IMPL1(RectangleGeometry), CTOR_IMPL1(PathGeometry), CTOR_IMPL1(SolidColorBrush), CTOR_IMPL1(Pen)
{
}

#define CREATERESOURCE_IMPL1(T) 	   \
case RT_##T:						   \
container = _container##T;			   \
handle = container->Allocate();		   \
_added##T.emplace_back(handle);		   \
break;								   

HRESULT ResourceManagerBase::CreateResource(ResourceType resType, IResource ** res)
{
	try
	{
		UINT_PTR handle;
		std::shared_ptr<IResourceContainer> container = nullptr;
		switch (resType)
		{
			CREATERESOURCE_IMPL1(LineGeometry);
			CREATERESOURCE_IMPL1(RectangleGeometry);
			CREATERESOURCE_IMPL1(PathGeometry);
			CREATERESOURCE_IMPL1(SolidColorBrush);
			CREATERESOURCE_IMPL1(Pen);
		default:
			break;
		}
		if (container)
		{
			*res = Make<ResourceRef>(std::move(container), resType, handle).Detach();
			return S_OK;
		}
		return E_INVALIDARG;
	}
	CATCH_ALL();
}

#define UPDATE_RES_IMPL1_PRE(T)											\
try																		\
{																		\
	auto handle = static_cast<ResourceRef*>(res)->GetHandle();			\
	assert(static_cast<ResourceRef*>(res)->GetType() == RT_##T);			\
	auto& resObj = _container##T->FindResource(handle);

#define UPDATE_RES_IMPL1_AFT(T)											\
_updated##T.emplace_back(handle);										\
{																			   \
	auto& ddcls = resObj.GetDependentDrawCallLists(); \
	for (auto it = ddcls.begin(); it != ddcls.end(); ++it)					   \
	{																		   \
		if (auto dcl = it->lock())											   \
			_updatedDrawCallList.emplace_back(std::move(dcl));				   \
		else																   \
			it = ddcls.erase(it);											   \
	}																		   \
}																			   \
return S_OK;															\
}																		\
CATCH_ALL()

HRESULT ResourceManagerBase::UpdateLineGeometry(IResource * res, LineGeometryData * data)
{
	UPDATE_RES_IMPL1_PRE(LineGeometry)
		resObj.Data = *data;
	UPDATE_RES_IMPL1_AFT(LineGeometry);
}

HRESULT ResourceManagerBase::UpdateRectangleGeometry(IResource * res, RectangleGeometryData * data)
{
	UPDATE_RES_IMPL1_PRE(RectangleGeometry)
		resObj.Data = *data;
	UPDATE_RES_IMPL1_AFT(RectangleGeometry);
}

namespace
{
	template<class T>
	T* Read(byte*& data)
	{
		auto old = reinterpret_cast<T*>(data);
		data += sizeof(T);
		return reinterpret_cast<T*>(old);
	}

#define UPDATE_PATH_GEO_IMPL1(T)											\
case T:																		\
	seg.Operation = T;														\
	seg.Data.##T## = *Read<Segment::tagData::tag##T>(data);					\
	break;
}

STDMETHODIMP ResourceManagerBase::UpdatePathGeometry(IResource * res, byte * data, UINT32 length)
{
	UPDATE_RES_IMPL1_PRE(PathGeometry)
		using namespace PathGeometrySegments;

	auto& segments = resObj.Segments;
	segments.clear();
	const auto end = data + length;
	while (data < end)
	{
		Segment seg;
		const auto type = Read<Operations>(data);
		switch (*type)
		{
			UPDATE_PATH_GEO_IMPL1(MoveTo);
			UPDATE_PATH_GEO_IMPL1(LineTo);
			UPDATE_PATH_GEO_IMPL1(ArcTo);
		default:
			continue;
		}
		segments.emplace_back(std::move(seg));
	}
	UPDATE_RES_IMPL1_AFT(PathGeometry);
}

HRESULT ResourceManagerBase::UpdateSolidColorBrush(IResource * res, ColorF * color)
{
	UPDATE_RES_IMPL1_PRE(SolidColorBrush)
		resObj.Color = *color;
	UPDATE_RES_IMPL1_AFT(SolidColorBrush);
}

HRESULT ResourceManagerBase::UpdatePen(IResource * res, float thickness, IResource * brush)
{
	UPDATE_RES_IMPL1_PRE(Pen)
		resObj.Thickness = thickness;
	resObj.Brush = static_cast<ResourceRef*>(brush);
	UPDATE_RES_IMPL1_AFT(Pen);
}

Brush & ResourceManagerBase::GetBrush(UINT_PTR handle)
{
	return _containerSolidColorBrush->FindResource(handle);
}

const Brush & ResourceManagerBase::GetBrush(UINT_PTR handle) const
{
	return _containerSolidColorBrush->FindResource(handle);
}

HRESULT ResourceManagerBase::CreateRenderCommandBuffer(IRenderCommandBuffer ** buffer)
{
	try
	{
		*buffer = Make<RenderCommandBuffer>(this).Detach();
		return S_OK;
	}
	CATCH_ALL();
}

#define UPDATE_IMPL1(T)			 \
_added##T.clear();				 \
_updated##T.clear();			 \
_container##T->CleanUp();		 

#define UPDATE_IMPL2(T)									\
auto& trc##T = Get##T##TRC();							\
trc##T.Add(_added##T, *_container##T.get());			\
trc##T.Update(_updated##T, *_container##T.get());		\
trc##T.Remove(_container##T->GetCleanupList());

void ResourceManagerBase::Update()
{
	{
		UPDATE_IMPL2(LineGeometry);
		UPDATE_IMPL1(LineGeometry);
	}
	{
		UPDATE_IMPL2(RectangleGeometry);
		UPDATE_IMPL1(RectangleGeometry);
	}
	{
		UPDATE_IMPL2(PathGeometry);
		UPDATE_IMPL1(PathGeometry);
	}
	{
		UPDATE_IMPL1(SolidColorBrush);
	}
	{
		UPDATE_IMPL1(Pen);
	}
	{
		for (auto&& dcl : _updatedDrawCallList)
			dcl->Update();
		_updatedDrawCallList.clear();
	}
	UpdateOverride();
}

#define ADDCL_IMPL1(T)															\
	case RT_##T:																\
		Get##T(resRef->GetHandle()).AddDependentDrawCallList(std::move(dcl));	\
		break;

void ResourceManagerBase::AddDependentDrawCallList(std::weak_ptr<IDrawCallList>&& dcl, IResource* res)
{
	if (!res) return;
	auto resRef = static_cast<ResourceRef*>(res);
	switch (resRef->GetType())
	{
		ADDCL_IMPL1(LineGeometry);
		ADDCL_IMPL1(RectangleGeometry);
		ADDCL_IMPL1(PathGeometry);
		ADDCL_IMPL1(Pen);
	default:
		break;
	}
}
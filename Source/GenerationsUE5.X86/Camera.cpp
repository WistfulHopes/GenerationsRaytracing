#include "Pch.h"
#include "Camera.h"

#include "Message.h"
#include "MessageSender.h"

HOOK(void, __fastcall, UpdateCamera, 0x10FB770, Sonic::CCamera* This, void* Edx, uint32_t a2)
{
    originalUpdateCamera(This, Edx, a2);
    
    const auto pos = This->m_Position;
    const auto tgt = This->m_TargetPosition;
    const auto up = This->m_UpVector;

    auto& message = s_messageSender.makeMessage<MsgCameraUpdate>();

    message.posX = pos.x();
    message.posY = pos.y();
    message.posZ = pos.z();
    
    message.tgtX = tgt.x();
    message.tgtY = tgt.y();
    message.tgtZ = tgt.z();
    
    message.upX = up.x();
    message.upY = up.y();
    message.upZ = up.z();

    message.fov = This->m_FieldOfView;
    message.aspectRatio = This->m_MyCamera.m_AspectRatio;

    s_messageSender.endMessage();
}

void InstallCamera::applyPatches()
{
    INSTALL_HOOK(UpdateCamera);
}

#include "Pch.h"
#include "SonicPlayer.h"

#include "Message.h"
#include "MessageSender.h"

HOOK(void, __stdcall, CPlayerSpeedContext_Ctor, 0xE668B0, Sonic::Player::CPlayerSpeed* This, void* Unk)
{
	originalCPlayerSpeedContext_Ctor(This, Unk);

	bool bIsModern = (Sonic::Player::CSonicClassicContext::GetInstance() == nullptr) && (Sonic::Player::CSonicSpContext::GetInstance() == nullptr);

	auto& message = s_messageSender.makeMessage<MsgSonicInit>();

	message.bIsModern = bIsModern;

	s_messageSender.endMessage();
}

HOOK(void, __fastcall, CPlayerSpeedUpdate, 0xE6BF20, Sonic::Player::CPlayerSpeed* This, void* Edx, const hh::fnd::SUpdateInfo& updateInfo)
{
	originalCPlayerSpeedUpdate(This, Edx, updateInfo);

	auto animationPose = This->m_spAnimationPose;
	auto skeleton = (hk2010_2_0::hkaSkeleton*)This->m_spAnimationPose->m_pAnimData->m_pSkeleton;
	
    auto sonic = This->GetContext();
	bool bIsSuper = sonic->m_pStateFlag->m_Flags[sonic->eStateFlag_InvokeSuperSonic];

	auto& message = s_messageSender.makeMessage<MsgSonicUpdate>();

	message.bIsSuper = bIsSuper;
	const auto mat = sonic->m_spMatrixNode->GetWorldMatrix();

	message.matrix[0] = mat.data()[0];
	message.matrix[1] = mat.data()[1];
	message.matrix[2] = mat.data()[2];
	message.matrix[3] = mat.data()[3];
	message.matrix[4] = mat.data()[4];
	message.matrix[5] = mat.data()[5];
	message.matrix[6] = mat.data()[6];
	message.matrix[7] = mat.data()[7];
	message.matrix[8] = mat.data()[8];
	message.matrix[9] = mat.data()[9];
	message.matrix[10] = mat.data()[10];
	message.matrix[11] = mat.data()[11];
	message.matrix[12] = mat.data()[12];
	message.matrix[13] = mat.data()[13];
	message.matrix[14] = mat.data()[14];
	message.matrix[15] = mat.data()[15];

	if (!animationPose) return;

	message.boneCount = animationPose->m_numBones;
	
	for (int i = 0; i < message.boneCount; i++)
	{
		Hedgehog::Math::CMatrix boneMat;
		
		message.boneNames[i][0] = '\0';
		strncat(message.boneNames[i], skeleton->m_bones.GetIndex(i)->m_name.cString(), 0xFF);
		
		auto boneTransform = animationPose->m_pAnimData->m_TransformArray.GetIndex(i);

		message.boneTransforms[i].posX = boneTransform->m_Position.x();
		message.boneTransforms[i].posY = boneTransform->m_Position.y();
		message.boneTransforms[i].posZ = boneTransform->m_Position.z();

		message.boneTransforms[i].rotX = boneTransform->m_Rotation.x();
		message.boneTransforms[i].rotY = boneTransform->m_Rotation.y();
		message.boneTransforms[i].rotZ = boneTransform->m_Rotation.z();
		message.boneTransforms[i].rotW = boneTransform->m_Rotation.w();

		message.boneTransforms[i].sclX = boneTransform->m_Scale.x();
		message.boneTransforms[i].sclY = boneTransform->m_Scale.y();
		message.boneTransforms[i].sclZ = boneTransform->m_Scale.z();
	}
	
	s_messageSender.endMessage();
}

void InstallSonicPlayer::applyPatches()
{
	INSTALL_HOOK(CPlayerSpeedContext_Ctor);
	INSTALL_HOOK(CPlayerSpeedUpdate);
}

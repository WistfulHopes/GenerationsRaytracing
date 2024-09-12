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
		message.boneNames[i][0] = '\0';
		strncat(message.boneNames[i], animationPose->m_spModelData->m_spNodes[i].m_Name.c_str(), 0xFF);
		
		auto boneMat = animationPose->GetMatrixList()[i];

		message.boneMatrices[i][0] = boneMat.data()[0];
		message.boneMatrices[i][1] = boneMat.data()[1];
		message.boneMatrices[i][2] = boneMat.data()[2];
		message.boneMatrices[i][3] = boneMat.data()[3];
		message.boneMatrices[i][4] = boneMat.data()[4];
		message.boneMatrices[i][5] = boneMat.data()[5];
		message.boneMatrices[i][6] = boneMat.data()[6];
		message.boneMatrices[i][7] = boneMat.data()[7];
		message.boneMatrices[i][8] = boneMat.data()[8];
		message.boneMatrices[i][9] = boneMat.data()[9];
		message.boneMatrices[i][10] = boneMat.data()[10];
		message.boneMatrices[i][11] = boneMat.data()[11];
		message.boneMatrices[i][12] = boneMat.data()[12];
		message.boneMatrices[i][13] = boneMat.data()[13];
		message.boneMatrices[i][14] = boneMat.data()[14];
		message.boneMatrices[i][15] = boneMat.data()[15];
	}
	
	s_messageSender.endMessage();
}

void InstallSonicPlayer::applyPatches()
{
	INSTALL_HOOK(CPlayerSpeedContext_Ctor);
	INSTALL_HOOK(CPlayerSpeedUpdate);
}

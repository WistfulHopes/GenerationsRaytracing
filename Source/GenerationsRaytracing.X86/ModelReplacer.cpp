#include "ModelReplacer.h"

#include "ModelData.h"
#include "SampleChunkResource.h"
#include "InstanceData.h"
#include "MaterialData.h"
#include "Logger.h"

struct NoAoModel
{
    std::string name;
    std::string noAoName;
    std::vector<std::string> archiveNames;
    std::vector<XXH32_hash_t> hashes;
};

static std::vector<NoAoModel> s_noAoModels;

static void parseJson(json& json)
{
    for (auto& obj : json)
    {
        auto& noAoModel = s_noAoModels.emplace_back();

        noAoModel.name = obj["name"];
        noAoModel.noAoName = obj["no_ao_name"];

        for (auto& archiveNameObj : obj["archive_names"])
            noAoModel.archiveNames.push_back(archiveNameObj);

        for (auto& hashObj : obj["hashes"])
        {
            if (hashObj.is_number_unsigned())
            {
                noAoModel.hashes.push_back(hashObj);
            }
            else
            {
                std::string value = hashObj;

                if (value.size() > 2 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X'))
                    value = value.substr(2);

                noAoModel.hashes.push_back(std::stoul(value, nullptr, 16));
            }
        }
    }
}

static Mutex s_mutex;

struct PendingModel
{
    uint32_t noAoModelIndex;
    boost::shared_ptr<Hedgehog::Mirage::CModelData> modelData;
    boost::shared_ptr<Hedgehog::Database::CDatabase> database;
};

static std::vector<PendingModel> s_pendingModels;

HOOK(void, __cdecl, ModelDataMake, 0x7337A0,
    const Hedgehog::Base::CSharedString& name,
    void* data,
    uint32_t dataSize,
    const boost::shared_ptr<Hedgehog::Database::CDatabase>& database,
    Hedgehog::Mirage::CRenderingInfrastructure* renderingInfrastructure)
{
    if (data != nullptr)
    {
        for (size_t i = 0; i < s_noAoModels.size(); i++)
        {
            auto& noAoModel = s_noAoModels[i];

            if (noAoModel.name == name.c_str())
            {
                Hedgehog::Mirage::CMirageDatabaseWrapper wrapper(database.get());

                const auto modelData = wrapper.GetModelData(name);
                if (!modelData->IsMadeOne())
                {
                    auto& modelDataEx = *reinterpret_cast<ModelDataEx*>(modelData.get());
                    const XXH32_hash_t hash = XXH32(data, dataSize, 0);

                    if (std::find(noAoModel.hashes.begin(), noAoModel.hashes.end(), hash) != noAoModel.hashes.end())
                    {
                        LockGuard lock(s_mutex);
                        s_pendingModels.push_back({ i, modelData, database });
                    }
                }

                break;
            }
        }
    }

    originalModelDataMake(name, data, dataSize, database, renderingInfrastructure);
}

static std::unordered_set<std::string_view> s_archiveNames;

void ModelReplacer::createPendingModels()
{
    // Must make a local copy to avoid deadlock when calling LoadArchive
    s_mutex.lock();
    const auto pendingModels = s_pendingModels;
    s_pendingModels.clear();
    s_mutex.unlock();

    if (!pendingModels.empty())
    {
        auto database = Hedgehog::Database::CDatabase::CreateDatabase();

        for (auto& pendingModel : pendingModels)
        {
            for (auto& archiveName : s_noAoModels[pendingModel.noAoModelIndex].archiveNames)
                s_archiveNames.emplace(archiveName);
        }

        auto& loader = Sonic::CApplicationDocument::GetInstance()->m_pMember->m_spDatabaseLoader;

        static Hedgehog::Base::CSharedString s_ar(".ar");
        static Hedgehog::Base::CSharedString s_arl(".arl");

        for (const auto& archiveName : s_archiveNames)
        {
            loader->CreateArchiveList(
                archiveName.data() + s_ar,
                archiveName.data() + s_arl,
                { 200, 5 });
        }

        for (const auto& archiveName : s_archiveNames)
            loader->LoadArchiveList(database, archiveName.data() + s_arl);

        for (const auto& archiveName : s_archiveNames)
            loader->LoadArchive(database, archiveName.data() + s_ar, {-10, 5}, false, false);

        Hedgehog::Mirage::CMirageDatabaseWrapper wrapper(database.get());

        for (auto& pendingModel : pendingModels)
        {
            const auto modelDataEx = reinterpret_cast<ModelDataEx*>(pendingModel.modelData.get());
            modelDataEx->m_noAoModel = wrapper.GetModelData(s_noAoModels[pendingModel.noAoModelIndex].noAoName.c_str());
        }

        const auto databaseDataNames = database->GetDatabaseDataNames();

        for (const auto& fhlName : databaseDataNames)
        {
            static constexpr char s_mirageMaterial[] = "Mirage.material";

            if (strstr(fhlName.c_str(), s_mirageMaterial) == fhlName.data())
            {
                const char* fhlSuffix = strstr(fhlName.c_str(), "_fhl");

                if (fhlSuffix == (fhlName.data() + fhlName.size() - 4))
                {
                    const auto name = fhlName.substr(sizeof(s_mirageMaterial),
                        fhlName.size() - sizeof(s_mirageMaterial) - 4);

                    bool found = false;

                    for (auto& pendingModel : pendingModels)
                    {
                        const auto materialData = Hedgehog::Mirage::CMirageDatabaseWrapper(
                            pendingModel.database.get()).GetMaterialData(name);

                        if (materialData != nullptr)
                        {
                            auto& materialDataEx = *reinterpret_cast<MaterialDataEx*>(materialData.get());

                            materialDataEx.m_fhlMaterial = wrapper.GetMaterialData(
                                fhlName.substr(sizeof(s_mirageMaterial)));

                            found = true;

                            break;
                        }
                    }

                    if (!found)
                    {
                        Logger::logFormatted(LogType::Error, "Unable to locate \"%s.material\" for \"%s.material\"", 
                            name.c_str(), fhlName.c_str() + sizeof(s_mirageMaterial));
                    }
                }
            }
        }

        s_archiveNames.clear();
    }
}

static std::unordered_map<Hedgehog::Mirage::CMaterialData*, Hedgehog::Mirage::CMaterialData*> s_fhlMaterials;

static FUNCTION_PTR(void, __thiscall, cloneMaterial, 0x704CE0,
    Hedgehog::Mirage::CMaterialData* This, Hedgehog::Mirage::CMaterialData* rValue);

void ModelReplacer::processFhlMaterials(InstanceInfoEx& instanceInfoEx, const MaterialMap& materialMap)
{
    static Hedgehog::Base::CStringSymbol s_texcoordOffsetSymbol("mrgTexcoordOffset");

    for (auto& [key, value] : instanceInfoEx.m_effectMap)
    {
        auto& keyEx = *reinterpret_cast<MaterialDataEx*>(key);
        if (keyEx.m_fhlMaterial != nullptr)
            s_fhlMaterials.emplace(keyEx.m_fhlMaterial.get(), key);
    }

    for (auto& [key, value] : materialMap)
    {
        auto& keyEx = *reinterpret_cast<MaterialDataEx*>(key);
        if (keyEx.m_fhlMaterial != nullptr)
            s_fhlMaterials.emplace(keyEx.m_fhlMaterial.get(), key);
    }

    for (auto& [fhlMaterial, material] : s_fhlMaterials)
    {
        auto overrideFindResult = materialMap.find(material);
        if (overrideFindResult != materialMap.end())
            material = overrideFindResult->second.get();

        auto effectFindResult = instanceInfoEx.m_effectMap.find(material);
        if (effectFindResult != instanceInfoEx.m_effectMap.end())
            material = effectFindResult->second.get();

        auto& materialClone = instanceInfoEx.m_effectMap[fhlMaterial];
        if (materialClone == nullptr)
        {
            const auto alsoMaterialClone = static_cast<Hedgehog::Mirage::CMaterialData*>(__HH_ALLOC(sizeof(MaterialDataEx)));
            Hedgehog::Mirage::fpCMaterialDataCtor(alsoMaterialClone);
            cloneMaterial(alsoMaterialClone, fhlMaterial);
            materialClone = boost::shared_ptr<Hedgehog::Mirage::CMaterialData>(alsoMaterialClone);
        }

        for (auto& sourceParam : material->m_Float4Params)
        {
            if (sourceParam->m_Name == s_texcoordOffsetSymbol)
            {
                bool found = false;

                for (auto& destParam : materialClone->m_Float4Params)
                {
                    if (destParam->m_Name == s_texcoordOffsetSymbol)
                    {
                        destParam = sourceParam;
                        found = true;
                        break;
                    }
                }

                if (!found)
                    materialClone->m_Float4Params.push_back(sourceParam);

                break;
            }
        }
    }

    s_fhlMaterials.clear();
}

void ModelReplacer::init()
{
    INSTALL_HOOK(ModelDataMake);

    std::ifstream stream("no_ao_models.json");
    if (stream.is_open())
    {
        json json;
        stream >> json;
        parseJson(json);

        stream.close();
    }
    else
    {
        MessageBox(nullptr, 
            TEXT("Unable to open \"no_ao_models.json\" in mod directory."), 
            TEXT("Generations Raytracing"), 
            MB_ICONERROR);
    }
}

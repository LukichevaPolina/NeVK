#include "ShaderManager.h"

#include <cstdio>

namespace nevk
{

ShaderManager::ShaderManager()
{
    mSlangSession = spCreateSession(NULL);
}

ShaderManager::~ShaderManager()
{
    if (mSlangSession)
    {
        spDestroySession(mSlangSession);
    }
}

ShaderManager::ShaderDesc ShaderManager::compileShader(const char* fileName, const char* entryPointName, Stage stage)
{
    SlangCompileRequest* slangRequest = spCreateCompileRequest(mSlangSession);

    // spSetDebugInfoLevel(slangRequest, SLANG_DEBUG_INFO_LEVEL_MAXIMAL);
    int targetIndex = spAddCodeGenTarget(slangRequest, SLANG_SPIRV);
    SlangProfileID profileID = spFindProfile(mSlangSession, "sm_6_3");
    spSetTargetProfile(slangRequest, targetIndex, profileID);
    SlangOptimizationLevel optLevel = SLANG_OPTIMIZATION_LEVEL_MAXIMAL;
    spSetOptimizationLevel(slangRequest, optLevel);
    int translationUnitIndex = spAddTranslationUnit(slangRequest, SLANG_SOURCE_LANGUAGE_SLANG, nullptr);
    spAddTranslationUnitSourceFile(slangRequest, translationUnitIndex, fileName);

    SlangStage slangStage = SLANG_STAGE_NONE;
    switch (stage)
    {
    case nevk::ShaderManager::Stage::eVertex:
        slangStage = SLANG_STAGE_VERTEX;
        break;
    case nevk::ShaderManager::Stage::ePixel:
        slangStage = SLANG_STAGE_PIXEL;
        break;
    case nevk::ShaderManager::Stage::eCompute:
        slangStage = SLANG_STAGE_COMPUTE;
        break;
    default:
        break;
    }
    int entryPointIndex = spAddEntryPoint(slangRequest, translationUnitIndex, entryPointName, slangStage);
    const SlangResult compileRes = spCompile(slangRequest);

    if (auto diagnostics = spGetDiagnosticOutput(slangRequest))
        printf("%s\n", diagnostics);
    if (SLANG_FAILED(compileRes))
    {
        spDestroyCompileRequest(slangRequest);
        return ShaderDesc();
    }

    size_t dataSize = 0;
    void const* data = spGetEntryPointCode(slangRequest, entryPointIndex, &dataSize);
    if (!data)
        return ShaderDesc();

    ShaderDesc desc{};

    desc.fileName = std::string(fileName);
    desc.entryPointName = std::string(entryPointName);
    desc.stage = stage;

    desc.code.resize(dataSize);
    memcpy(&desc.code[0], data, dataSize);
    desc.codeSize = dataSize;
    desc.type = slangStage;
    desc.slangReflection = (slang::ShaderReflection*)spGetReflection(slangRequest);
    desc.slangRequest = slangRequest;


    //spDestroyCompileRequest(slangRequest);
    return desc;
}
uint32_t ShaderManager::loadShader(const char* fileName, const char* entryPointName, Stage stage)
{
    ShaderDesc sd = compileShader(fileName, entryPointName, stage);
    uint32_t shaderId = 0;

    // Try to reload existing shader
    for (; shaderId < mShaderDescs.size(); shaderId++)
    {
        ShaderDesc& old_sd = mShaderDescs[shaderId];
        if (old_sd.fileName == std::string(fileName) && old_sd.entryPointName == std::string(entryPointName))
        {
            old_sd.code.resize(sd.codeSize);
            memcpy(&old_sd.code[0], &sd.code[0], sd.codeSize);
            old_sd.codeSize = sd.codeSize;
            return shaderId;
        }
    }

    // Remember new data
    shaderId = mShaderDescs.size();
    mShaderDescs.push_back(sd);

    return shaderId;
}
void ShaderManager::reloadAllShaders()
{
    for (const auto& shader : mShaderDescs)
    {
        loadShader(shader.fileName.c_str(), shader.entryPointName.c_str(), shader.stage);
    }
}
bool ShaderManager::getShaderCode(uint32_t id, const char*& code, uint32_t& size)
{
    if (mShaderDescs.size() <= id)
    {
        return false;
    }

    ShaderDesc& sd = mShaderDescs[id];
    code = sd.code.data();
    size = sd.codeSize;

    return true;
}

void print(slang::VariableLayoutReflection* parameter);
void print(slang::TypeLayoutReflection* parameter);

void printResource(slang::TypeLayoutReflection* typeLayout)
{
    assert(typeLayout->getKind() == slang::TypeReflection::Kind::Resource);
    // slang::TypeReflection* type = parameter->getType();
    printf("type: %s\t", typeLayout->getName());
    printf("\n");
}

void printStruct(slang::TypeLayoutReflection* typeLayout)
{
    assert(typeLayout->getKind() == slang::TypeReflection::Kind::Struct);
    printf("struct name: %s\n", typeLayout->getName());
    printf("field count: %d\n", typeLayout->getFieldCount());
    for (int i = 0; i < typeLayout->getFieldCount(); ++i)
    {
        auto field = typeLayout->getFieldByIndex(i);
        printf("field offset: %d\t", (uint32_t)field->getOffset());
        print(typeLayout->getFieldByIndex(i));
    }
}

void printParameterBlock(slang::TypeLayoutReflection* typeLayout)
{
    printf("%s\n", typeLayout->getName());
    size_t primaryConstantBufferSize = typeLayout->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM);
    printf("const buffer size: %d\n", (uint32_t)primaryConstantBufferSize);
    auto elem = typeLayout->getElementTypeLayout();
    if (elem == nullptr)
    {
        return;
    }
    print(elem);
}
void print(slang::TypeLayoutReflection* typeLayout)
{
    switch (typeLayout->getKind())
    {
    case slang::TypeReflection::Kind::Struct: {
        printStruct(typeLayout);
        break;
    }
    case slang::TypeReflection::Kind::Resource: {
        printResource(typeLayout);
        break;
    }
    case slang::TypeReflection::Kind::ParameterBlock: {
        printParameterBlock(typeLayout);
        break;
    }
    case slang::TypeReflection::Kind::Array: {
        printf("array");
        printf(" %s", typeLayout->getType()->getElementType()->getName());
        printf("[%d]\n", (uint32_t)typeLayout->getElementCount());
        break;
    }
    default:
        break;
    }
}

void print(slang::VariableLayoutReflection* var)
{
    const char* name = var->getName();
    printf("name: %s\t", name);
    slang::TypeLayoutReflection* typeLayout = var->getTypeLayout();
    auto categoryCount = var->getCategoryCount();
    if (categoryCount)
    {
        for (uint32_t cc = 0; cc < categoryCount; ++cc)
        {
            auto category = var->getCategoryByIndex(cc);
            auto index = var->getOffset(category);
            auto space = var->getBindingSpace(category);
            auto count = typeLayout->getSize(category);
            if (category == SLANG_PARAMETER_CATEGORY_UNIFORM)
            {
                printf("offset=%d, size=%d\n", (uint32_t)index, (uint32_t)count);
            }
            else
            {
                printf("binding=%d, set=%d\n", (uint32_t)index, (uint32_t)space);
            }
        }
    }
    print(typeLayout);
}

void ShaderManager::printInfo(uint32_t id)
{
    if (mShaderDescs.size() <= id)
    {
        return;
    }
    ShaderDesc& desc = mShaderDescs[id];
    uint32_t paramCount = desc.slangReflection->getParameterCount();
    printf("%d \n", paramCount);
    for (uint32_t i = 0; i < paramCount; ++i)
    {
        slang::VariableLayoutReflection* parameter = desc.slangReflection->getParameterByIndex(i);
        // unsigned index = parameter->getOffset();
        // unsigned space = parameter->getBindingSpace();
        // printf("binding=%d, set=%d\n", index, space);
        print(parameter);
    }
}

} // namespace nevk

#include "AutoJumpTool.hpp"


AutoJumpTool autoJumpTool("autojump");

void AutoJumpTool::Apply(TasFramebulk& bulk)
{
    auto ttParams = std::static_pointer_cast<AutoJumpToolParams>(this->params);
}

AutoJumpTool* AutoJumpTool::GetTool()
{
    return &autoJumpTool;
}

std::shared_ptr<TasToolParams> AutoJumpTool::ParseParams(std::vector<std::string> vp)
{
    if (vp.empty())
        return nullptr;

    bool arg = vp[0] == "on";

    return std::make_shared<AutoJumpToolParams>(arg);
}

void AutoJumpTool::Reset()
{
    this->params = std::make_shared<AutoJumpToolParams>();
}

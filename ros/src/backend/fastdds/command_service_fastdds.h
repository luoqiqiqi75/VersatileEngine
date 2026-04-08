#pragma once

#include "participant.h"

namespace ve::ros::fastdds {

class VE_API FastDdsCommandService
{
    VE_DECLARE_PRIVATE

public:
    explicit FastDdsCommandService(Participant& participant, const std::string& prefix = "ve");
    ~FastDdsCommandService();

    void start();
    void stop();
    bool isRunning() const;
};

} // namespace ve::ros::fastdds

// ----------------------------------------------------------------------------
// main.cpp — veqt_test entry (QCoreApplication + ve_test registry)
// ----------------------------------------------------------------------------

#include "ve_test.h"

#include <QCoreApplication>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    return VE_RUN_ALL();
}

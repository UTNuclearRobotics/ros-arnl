#ifndef _ARNLSYSTEM_H
#define _ARNLSYSTEM_H

#include "ariaUtil.h"
#include "RobotMonitor.h"
#include <iostream>

class ArRobot;
class ArPathPlanningTask;
class ArServerModeRatioDrive;
class ArLocalizationTask;
class ArServerModeStop;
class ArServerModeGoto;
class ArServerModeWander; //
class ArServerModeDock; //
class ArRobotPacket;
class ArServerBase;
class ArMap;
class ArServerInfoDrawings;

class ArnlSystem
{
  public:
    ArnlSystem(const char *_logprefix = "");
    virtual ~ArnlSystem();
       
  public:

    typedef enum {
      OK,
      RobotConnectError,
      ParseArgumentsError,
      ConfigError,
      LaserConnectError
    } Error;

    Error setup();

    ArRobot *robot;
    ArPathPlanningTask *pathTask;
    ArServerModeRatioDrive *modeRatioDrive;
    ArLocalizationTask *locTask;
    ArServerModeStop *modeStop;
    ArServerModeGoto *modeGoto;
    ArServerModeWander *modeWander; //
    ArServerModeDock *modeDock; //
    ArServerBase *serverBase;
    ArMap *map;
    ArTime creationTime;
    ArServerInfoDrawings *drawings;
    RobotMonitor *monitor;

    const char* getServerMode() const ;
    const char* getServerStatus() const ;
    const char* getPathStateName() const ;
    bool setMap(std::string mapFile);
    
  protected:
    const char *logprefix;
    bool handleDebugMessage(ArRobotPacket *pkt);
};

#endif

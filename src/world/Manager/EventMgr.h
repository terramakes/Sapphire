#pragma once

#include <cstdint>
#include <string>
#include <ForwardsZone.h>
#include "Event/EventHandler.h"

namespace Sapphire::World::Manager
{

  class EventMgr
  {
  public:
    EventMgr() = default;

    std::string getEventName( uint32_t eventId );
    std::string getErrorCodeName( uint8_t errorCode );

    uint32_t mapEventActorToRealActor( uint32_t eventActorId );

    void handleReturnEventScene( Entity::Player& player, uint32_t eventId, uint16_t sceneId, uint8_t errorCode,
                                 uint8_t numOfResults, const std::vector< uint32_t >& results );

    void handleReturnStringEventScene( Entity::Player& player, uint32_t eventId, uint16_t sceneId, const std::string& resultString );


    void checkEvent( Entity::Player& player, uint32_t eventId );
    void eventFinish( Entity::Player& player, uint32_t eventId, uint32_t freePlayer );
    void eventStart( Entity::Player& player, uint64_t actorId, uint32_t eventId, Event::EventHandler::EventType eventType, uint8_t eventParam1,
                     uint32_t eventParam2, Event::EventHandler::EventFinishCallback callback = nullptr );
    void eventActionStart( Entity::Player& player, uint32_t eventId, uint32_t action, World::Action::ActionCallback finishCallback,
                           World::Action::ActionCallback interruptCallback, uint64_t additional );
    /*! start an event item action */
    void eventItemActionStart( Entity::Player& player, uint32_t eventId, uint32_t action, World::Action::ActionCallback finishCallback,
                               World::Action::ActionCallback interruptCallback, uint64_t additional );

    void playGilShop( Entity::Player& player, uint32_t eventId, uint32_t flags, uint32_t param1, Event::EventHandler::SceneReturnCallback eventCallback );

    /*! play a subevent */
    void playScene( Entity::Player& player, uint32_t eventId, uint32_t scene, uint32_t flags, std::vector< uint32_t > values,
                    Event::EventHandler::SceneReturnCallback eventReturnCallback = nullptr );

    /*! resume a subevent */
    void resumeScene( Entity::Player& player, uint32_t eventId, uint32_t scene, std::vector< uint32_t > values );

    /*! play a subevent */
    void playScene( Entity::Player& player, uint32_t eventId, uint32_t scene, uint32_t flags,
                    Event::EventHandler::SceneReturnCallback eventReturnCallback = nullptr );

    void playQuestScene( Entity::Player& player, uint32_t eventId, uint32_t scene, uint32_t flags,
                         Event::EventHandler::QuestSceneReturnCallback eventReturnCallback = nullptr );

    /*! play a subevent */
    void playSceneChain( Entity::Player& player, uint32_t eventId, uint32_t scene, uint32_t flags,
                         std::vector< uint32_t > values,
                         Event::EventHandler::SceneChainCallback sceneChainCallback = nullptr );

    /*! play a subevent */
    void playSceneChain( Entity::Player& player, uint32_t eventId, uint32_t scene, uint32_t flags,
                         Event::EventHandler::SceneChainCallback sceneChainCallback = nullptr );

    void playQuestSceneChain( Entity::Player& player, uint32_t eventId, uint32_t scene, uint32_t flags,
                              Event::EventHandler::QuestSceneChainCallback sceneChainCallback = nullptr );

    bool sendEventPlay( Entity::Player& player, uint32_t eventid, uint32_t scene, uint32_t flags );

    /*! setup the event and return a ptr to it */
    Event::EventHandlerPtr bootstrapSceneEvent( Entity::Player& player, uint32_t eventId, uint32_t flags );
  };

}

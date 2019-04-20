#include <Util/Util.h>
#include <Util/UtilMath.h>
#include <Network/PacketContainer.h>
#include <Exd/ExdDataGenerated.h>
#include <utility>
#include <Network/CommonActorControl.h>
#include <Network/PacketWrappers/EffectPacket.h>
#include <Network/PacketDef/Zone/ClientZoneDef.h>
#include <Logging/Logger.h>

#include "Forwards.h"
#include "Action/Action.h"

#include "Territory/Zone.h"

#include "Network/GameConnection.h"
#include "Network/PacketWrappers/ActorControlPacket142.h"
#include "Network/PacketWrappers/ActorControlPacket143.h"
#include "Network/PacketWrappers/ActorControlPacket144.h"
#include "Network/PacketWrappers/UpdateHpMpTpPacket.h"
#include "Network/PacketWrappers/NpcSpawnPacket.h"
#include "Network/PacketWrappers/MoveActorPacket.h"
#include "Navi/NaviProvider.h"

#include "StatusEffect/StatusEffect.h"
#include "ServerMgr.h"
#include "Session.h"
#include "Math/CalcBattle.h"
#include "Chara.h"
#include "Player.h"
#include "BNpc.h"
#include "BNpcTemplate.h"
#include "Manager/TerritoryMgr.h"
#include "Common.h"
#include "Framework.h"
#include <Logging/Logger.h>
#include <Manager/NaviMgr.h>
#include <Manager/TerritoryMgr.h>
#include <Manager/RNGMgr.h>

using namespace Sapphire::Common;
using namespace Sapphire::Network::Packets;
using namespace Sapphire::Network::Packets::Server;
using namespace Sapphire::Network::ActorControl;

Sapphire::Entity::BNpc::BNpc( FrameworkPtr pFw ) :
  Npc( ObjKind::BattleNpc, pFw )
{
}

Sapphire::Entity::BNpc::BNpc( uint32_t id, BNpcTemplatePtr pTemplate, float posX, float posY, float posZ, float rot,
                              uint8_t level, uint32_t maxHp, ZonePtr pZone, FrameworkPtr pFw ) :
  Npc( ObjKind::BattleNpc, pFw )
{
  m_id = id;
  m_modelChara = pTemplate->getModelChara();
  m_displayFlags = pTemplate->getDisplayFlags();
  m_pose = pTemplate->getPose();
  m_aggressionMode = pTemplate->getAggressionMode();
  m_weaponMain = pTemplate->getWeaponMain();
  m_weaponSub = pTemplate->getWeaponSub();
  m_bNpcNameId = pTemplate->getBNpcNameId();
  m_bNpcBaseId = pTemplate->getBNpcBaseId();
  m_enemyType = pTemplate->getEnemyType();
  m_pos.x = posX;
  m_pos.y = posY;
  m_pos.z = posZ;
  m_rot = rot;
  m_level = level;
  m_invincibilityType = InvincibilityNone;
  m_currentStance = Common::Stance::Passive;
  m_levelId = 0;

  m_pCurrentZone = pZone;

  m_spawnPos = m_pos;

  m_timeOfDeath = 0;
  m_targetId = Common::INVALID_GAME_OBJECT_ID64;

  m_maxHp = maxHp;
  m_maxMp = 200;
  m_hp = maxHp;
  m_mp = 200;

  m_state = BNpcState::Idle;
  m_status = ActorStatus::Idle;

  m_baseStats.max_hp = maxHp;
  m_baseStats.max_mp = 200;

  memcpy( m_customize, pTemplate->getCustomize(), sizeof( m_customize ) );
  memcpy( m_modelEquip, pTemplate->getModelEquip(), sizeof( m_modelEquip ) );

  auto exdData = m_pFw->get< Data::ExdDataGenerated >();
  assert( exdData );

  auto bNpcBaseData = exdData->get< Data::BNpcBase >( m_bNpcBaseId );
  assert( bNpcBaseData );

  m_scale = bNpcBaseData->scale;

  auto modelChara = exdData->get< Data::ModelChara >( bNpcBaseData->modelChara );
  if( modelChara )
  {
    auto modelSkeleton = exdData->get< Data::ModelSkeleton >( modelChara->model );
    if( modelSkeleton )
      m_scale *= modelSkeleton->scaleFactor;
  }

  // todo: is this actually good?
  //m_naviTargetReachedDistance = m_scale * 2.f;
  m_naviTargetReachedDistance = 4.f;
}

Sapphire::Entity::BNpc::~BNpc() = default;

uint8_t Sapphire::Entity::BNpc::getAggressionMode() const
{
  return m_aggressionMode;
}

float Sapphire::Entity::BNpc::getNaviTargetReachedDistance() const
{
  return m_naviTargetReachedDistance;
}

uint8_t Sapphire::Entity::BNpc::getEnemyType() const
{
  return m_enemyType;
}

uint64_t Sapphire::Entity::BNpc::getWeaponMain() const
{
  return m_weaponMain;
}

uint64_t Sapphire::Entity::BNpc::getWeaponSub() const
{
  return m_weaponSub;
}

uint16_t Sapphire::Entity::BNpc::getModelChara() const
{
  return m_modelChara;
}

uint8_t Sapphire::Entity::BNpc::getLevel() const
{
  return m_level;
}

uint32_t Sapphire::Entity::BNpc::getBNpcBaseId() const
{
  return m_bNpcBaseId;
}

uint32_t Sapphire::Entity::BNpc::getBNpcNameId() const
{
  return m_bNpcNameId;
}

void Sapphire::Entity::BNpc::spawn( PlayerPtr pTarget )
{
  m_lastRoamTargetReached = Util::getTimeSeconds();
  pTarget->queuePacket( std::make_shared< NpcSpawnPacket >( *this, *pTarget ) );
}

void Sapphire::Entity::BNpc::despawn( PlayerPtr pTarget )
{
  pTarget->freePlayerSpawnId( getId() );
  pTarget->queuePacket( makeActorControl143( m_id, DespawnZoneScreenMsg, 0x04, getId(), 0x01 ) );
}

Sapphire::Entity::BNpcState Sapphire::Entity::BNpc::getState() const
{
  return m_state;
}

void Sapphire::Entity::BNpc::setState( BNpcState state )
{
  m_state = state;
}

void Sapphire::Entity::BNpc::step()
{
  if( m_naviLastPath.empty() )
    // No path to track
    return;

  auto stepPos = m_naviLastPath[ m_naviPathStep ];

  auto distanceToStep = Util::distance( getPos(), stepPos );
  auto distanceToDest = Util::distance( getPos(), m_naviTarget );

  if( distanceToStep <= 4 && m_naviPathStep < m_naviLastPath.size() - 1 )
  {
    // Reached step in path
    m_naviPathStep++;
    stepPos = m_naviLastPath[ m_naviPathStep ];
  }

  // This is probably not a good way to do it but works fine for now
  float angle = Util::calcAngFrom( getPos().x, getPos().z, stepPos.x, stepPos.z ) + PI;

  auto delta = static_cast< float >( Util::getTimeMs() - m_lastUpdate ) / 1000.f;

  float speed = 7.5f * delta;

  if( m_state == BNpcState::Roaming )
    speed *= 0.27f;

  // this seems to fix it but i don't know why :(
  if( speed > distanceToDest )
    speed = distanceToDest / delta;

  auto x = ( cosf( angle ) * speed );
  auto y = stepPos.y;
  auto z = ( sinf( angle ) * speed );


  face( stepPos );
  setPos( { getPos().x + x, y, getPos().z + z } );
  sendPositionUpdate();

}

bool Sapphire::Entity::BNpc::moveTo( const FFXIVARR_POSITION3& pos )
{

  auto pNaviProvider = m_pCurrentZone->getNaviProvider();

  if( !pNaviProvider )
  {
    Logger::error( "No NaviProvider for zone#{0} - {1}",
                   m_pCurrentZone->getGuId(),
                   m_pCurrentZone->getInternalName() );
    return false;
  }

  pNaviProvider->addAgentUpdateFlag( *this, DT_CROWD_OBSTACLE_AVOIDANCE );

  auto pos1 = pNaviProvider->getMovePos( *this );

  if( Util::distance( pos1, pos ) < getScale() )
  {
    // Reached destination
    face( pos );
    setPos( pos1 );
    sendPositionUpdate();
    //pNaviProvider->resetMoveTarget( *this );
    pNaviProvider->updateAgentPosition( *this );
    pNaviProvider->removeAgentUpdateFlag( *this, DT_CROWD_OBSTACLE_AVOIDANCE );
    return true;
  }

  m_pCurrentZone->updateActorPosition( *this );
  face( pos );
  setPos( pos1 );
  sendPositionUpdate();
  return false;
}

bool Sapphire::Entity::BNpc::moveTo( const Entity::Chara& targetChara )
{

  auto pNaviProvider = m_pCurrentZone->getNaviProvider();

  if( !pNaviProvider )
  {
    Logger::error( "No NaviProvider for zone#{0} - {1}",
                   m_pCurrentZone->getGuId(),
                   m_pCurrentZone->getInternalName() );
    return false;
  }

  pNaviProvider->addAgentUpdateFlag( *this, DT_CROWD_OBSTACLE_AVOIDANCE );

  auto pos1 = pNaviProvider->getMovePos( *this );

  if( Util::distance( pos1, targetChara.getPos() ) <= ( getScale() + targetChara.getScale() ) )
  {
    // Reached destination
    face( targetChara.getPos() );
    setPos( pos1 );
    sendPositionUpdate();
    //pNaviProvider->resetMoveTarget( *this );
    pNaviProvider->updateAgentPosition( *this );
    pNaviProvider->removeAgentUpdateFlag( *this, DT_CROWD_OBSTACLE_AVOIDANCE );
    return true;
  }

  m_pCurrentZone->updateActorPosition( *this );
  face( targetChara.getPos() );
  setPos( pos1 );
  sendPositionUpdate();
  return false;
}


void Sapphire::Entity::BNpc::sendPositionUpdate()
{
  uint8_t unk1 = 0x3a;
  uint8_t animationType = 2;

  if( m_state == BNpcState::Combat || m_state == BNpcState::Retreat )
    animationType = 0;

  auto movePacket = std::make_shared< MoveActorPacket >( *getAsChara(), unk1, animationType, 0, 0x5A );
  sendToInRangeSet( movePacket );
}

void Sapphire::Entity::BNpc::hateListClear()
{
  auto it = m_hateList.begin();
  for( auto listEntry : m_hateList )
  {
    if( isInRangeSet( listEntry->m_pChara ) )
      deaggro( listEntry->m_pChara );
  }
  m_hateList.clear();
}

Sapphire::Entity::CharaPtr Sapphire::Entity::BNpc::hateListGetHighest()
{
  auto it = m_hateList.begin();
  uint32_t maxHate = 0;
  std::shared_ptr< HateListEntry > entry;
  for( ; it != m_hateList.end(); ++it )
  {
    if( ( *it )->m_hateAmount > maxHate )
    {
      maxHate = ( *it )->m_hateAmount;
      entry = *it;
    }
  }

  if( entry && maxHate != 0 )
    return entry->m_pChara;

  return nullptr;
}

void Sapphire::Entity::BNpc::hateListAdd( Sapphire::Entity::CharaPtr pChara, int32_t hateAmount )
{
  auto hateEntry = std::make_shared< HateListEntry >();
  hateEntry->m_hateAmount = hateAmount;
  hateEntry->m_pChara = pChara;

  m_hateList.insert( hateEntry );
  if( pChara->isPlayer() )
  {
    auto pPlayer = pChara->getAsPlayer();
    pPlayer->hateListAdd( getAsBNpc() );
  }
}

void Sapphire::Entity::BNpc::hateListUpdate( Sapphire::Entity::CharaPtr pChara, int32_t hateAmount )
{
  for( auto listEntry : m_hateList )
  {
    if( listEntry->m_pChara == pChara )
    {
      listEntry->m_hateAmount += hateAmount;
      return;
    }
  }

  auto hateEntry = std::make_shared< HateListEntry >();
  hateEntry->m_hateAmount = hateAmount;
  hateEntry->m_pChara = pChara;
  m_hateList.insert( hateEntry );
}

void Sapphire::Entity::BNpc::hateListRemove( Sapphire::Entity::CharaPtr pChara )
{
  for( auto listEntry : m_hateList )
  {
    if( listEntry->m_pChara == pChara )
    {

      m_hateList.erase( listEntry );
      if( pChara->isPlayer() )
      {
        PlayerPtr tmpPlayer = pChara->getAsPlayer();
        tmpPlayer->onMobDeaggro( getAsBNpc() );
      }
      return;
    }
  }
}

bool Sapphire::Entity::BNpc::hateListHasActor( Sapphire::Entity::CharaPtr pChara )
{
  for( auto& listEntry : m_hateList )
  {
    if( listEntry->m_pChara == pChara )
      return true;
  }
  return false;
}

void Sapphire::Entity::BNpc::aggro( Sapphire::Entity::CharaPtr pChara )
{
  auto pRNGMgr = m_pFw->get< World::Manager::RNGMgr >();
  auto variation = static_cast< uint32_t >( pRNGMgr->getRandGenerator< float >( 50, 600 ).next() );

  m_lastAttack = Util::getTimeMs() + variation;
  hateListUpdate( pChara, 1 );

  changeTarget( pChara->getId() );
  setStance( Stance::Active );
  m_state = BNpcState::Combat;

  sendToInRangeSet( makeActorControl142( getId(), ActorControlType::ToggleWeapon, 1, 1, 0 ) );
  sendToInRangeSet( makeActorControl142( getId(), ActorControlType::ToggleAggro, 1, 0, 0 ) );

  if( pChara->isPlayer() )
  {
    PlayerPtr tmpPlayer = pChara->getAsPlayer();
    tmpPlayer->onMobAggro( getAsBNpc() );
  }

}

void Sapphire::Entity::BNpc::deaggro( Sapphire::Entity::CharaPtr pChara )
{
  if( !hateListHasActor( pChara ) )
    hateListRemove( pChara );

  if( pChara->isPlayer() )
  {
    PlayerPtr tmpPlayer = pChara->getAsPlayer();
    tmpPlayer->queuePacket( makeActorControl142( getId(), ActorControlType::ToggleWeapon, 0, 1, 1 ) );
    tmpPlayer->onMobDeaggro( getAsBNpc() );
  }
}

void Sapphire::Entity::BNpc::onTick()
{
  if( m_state == BNpcState::Retreat )
  {
    regainHp();
  }
}

void Sapphire::Entity::BNpc::update( uint64_t tickCount )
{
  const uint8_t minActorDistance = 4;
  const uint8_t maxDistanceToOrigin = 40;
  const uint32_t roamTick = 20;

  auto pNaviProvider = m_pCurrentZone->getNaviProvider();

  switch( m_state )
  {
    case BNpcState::Dead:
    case BNpcState::JustDied:
      return;

    case BNpcState::Retreat:
    {
      setInvincibilityType( InvincibilityType::InvincibilityIgnoreDamage );

      if( pNaviProvider )
      {
        pNaviProvider->setMoveTarget( *this, m_spawnPos );
      }

      if( moveTo( m_spawnPos ) )
      {
        setInvincibilityType( InvincibilityType::InvincibilityNone );

        // retail doesn't seem to roam straight after retreating
        // todo: perhaps requires more investigation?
        m_lastRoamTargetReached = Util::getTimeSeconds();

        // resetHp
        setHp( getMaxHp() );

        m_state = BNpcState::Idle;
        setOwner( nullptr );
      }
    }
    break;

    case BNpcState::Roaming:
    {

      if( pNaviProvider )
      {
        pNaviProvider->setMoveTarget( *this, m_roamPos );
      }

      if( moveTo( m_roamPos ) )
      {
        m_lastRoamTargetReached = Util::getTimeSeconds();
        m_state = BNpcState::Idle;
      }

      checkAggro();
    }
    break;

    case BNpcState::Idle:
    {
      auto pHatedActor = hateListGetHighest();
      if( pHatedActor )
        aggro( pHatedActor );

      if( pNaviProvider->syncPosToChara( *this ) )
        sendPositionUpdate();

      if( !hasFlag( Immobile ) && ( Util::getTimeSeconds() - m_lastRoamTargetReached > roamTick ) )
      {

        if( !pNaviProvider )
        {
          m_lastRoamTargetReached = Util::getTimeSeconds();
          break;
        }

        m_roamPos = pNaviProvider->findRandomPositionInCircle( m_spawnPos, 5 );
        m_state = BNpcState::Roaming;
      }

      checkAggro();
    }

    case BNpcState::Combat:
    {
      auto pHatedActor = hateListGetHighest();
      if( !pHatedActor )
        return;

      auto distanceOrig = Util::distance( getPos().x, getPos().y, getPos().z,
                                          m_spawnPos.x,
                                          m_spawnPos.y,
                                          m_spawnPos.z );

      if( pHatedActor && !pHatedActor->isAlive() )
      {
        hateListRemove( pHatedActor );
        pHatedActor = hateListGetHighest();
      }

      if( pNaviProvider->syncPosToChara( *this ) )
        sendPositionUpdate();

      if( pHatedActor )
      {
        auto distance = Util::distance( getPos().x, getPos().y, getPos().z,
                                        pHatedActor->getPos().x,
                                        pHatedActor->getPos().y,
                                        pHatedActor->getPos().z );

        if( !hasFlag( NoDeaggro ) && ( distanceOrig > maxDistanceToOrigin ) )
        {
          hateListClear();
          changeTarget( INVALID_GAME_OBJECT_ID64 );
          setStance( Stance::Passive );
          setOwner( nullptr );
          m_state = BNpcState::Retreat;
          break;
        }

        if( distance > ( getScale() / 2 + pHatedActor->getScale() / 2 ) )
        {
          if( hasFlag( Immobile ) )
            break;

          if( pNaviProvider )
          {
            pNaviProvider->setMoveTarget( *this, pHatedActor->getPos() );
          }
          moveTo( *pHatedActor );
        }
        else
        {
          if( !hasFlag( TurningDisabled ) && face( pHatedActor->getPos() ) )
            sendPositionUpdate();
          // in combat range. ATTACK!
          autoAttack( pHatedActor );
        }
      }
      else
      {
        changeTarget( INVALID_GAME_OBJECT_ID64 );
        setStance( Stance::Passive );
        //setOwner( nullptr );
        m_state = BNpcState::Retreat;
      }
    }
  }


  Chara::update( tickCount );
}

void Sapphire::Entity::BNpc::regainHp()
{
  if( this->m_hp < this->getMaxHp() )
  {
    auto addHp = static_cast< uint32_t >( this->getMaxHp() * 0.1f + 1 );

    if( this->m_hp + addHp < this->getMaxHp() )
      this->m_hp += addHp;
    else
      this->m_hp = this->getMaxHp();
  }

  this->sendStatusUpdate();
}

void Sapphire::Entity::BNpc::onActionHostile( Sapphire::Entity::CharaPtr pSource )
{
  if( !hateListGetHighest() )
    aggro( pSource );

  if( !m_pOwner )
    setOwner( pSource );
}

void Sapphire::Entity::BNpc::onDeath()
{
  setTargetId( INVALID_GAME_OBJECT_ID64 );
  m_currentStance = Stance::Passive;
  m_state = BNpcState::Dead;
  m_timeOfDeath = Util::getTimeSeconds();
  setOwner( nullptr );

  for( auto& pHateEntry : m_hateList )
  {
    // TODO: handle drops 
    auto pPlayer = pHateEntry->m_pChara->getAsPlayer();
    if( pPlayer )
      pPlayer->onMobKill( m_bNpcNameId );
  }
  hateListClear();
}

uint32_t Sapphire::Entity::BNpc::getTimeOfDeath() const
{
  return m_timeOfDeath;
}

void Sapphire::Entity::BNpc::setTimeOfDeath( uint32_t timeOfDeath )
{
  m_timeOfDeath = timeOfDeath;
}

void Sapphire::Entity::BNpc::checkAggro()
{
  // passive mobs should ignore players unless aggro'd
  if( m_aggressionMode == 1 )
    return;

  CharaPtr pClosestChara = getClosestChara();

  if( pClosestChara && pClosestChara->isAlive() && pClosestChara->isPlayer() )
  {
    // will use this range if chara level is lower than bnpc, otherwise diminishing equation applies
    float range = 13.f;

    if( pClosestChara->getLevel() > m_level )
    {
      auto levelDiff = std::abs( pClosestChara->getLevel() - this->getLevel() );

      if( levelDiff >= 10 )
        range = 0.f;
      else
        range = std::max< float >( 0.f, range - std::pow( 1.53f, levelDiff * 0.6f ) );
    }

    auto distance = Util::distance( getPos().x, getPos().y, getPos().z,
                                    pClosestChara->getPos().x,
                                    pClosestChara->getPos().y,
                                    pClosestChara->getPos().z );

    if( distance < range )
    {
      aggro( pClosestChara );
    }
  }
}

void Sapphire::Entity::BNpc::setOwner( Sapphire::Entity::CharaPtr m_pChara )
{
  m_pOwner = m_pChara;
  if( m_pChara != nullptr )
  {
    auto setOwnerPacket = makeZonePacket< FFXIVIpcActorOwner >( m_pChara->getId() );
    setOwnerPacket->data().type = 0x01;
    setOwnerPacket->data().actorId = m_pChara->getId();
    sendToInRangeSet( setOwnerPacket );
  }
  else
  {
    auto setOwnerPacket = makeZonePacket< FFXIVIpcActorOwner >( getId() );
    setOwnerPacket->data().type = 0x01;
    setOwnerPacket->data().actorId = INVALID_GAME_OBJECT_ID;
    sendToInRangeSet( setOwnerPacket );
  }
}

void Sapphire::Entity::BNpc::setLevelId( uint32_t levelId )
{
  m_levelId = levelId;
}

uint32_t Sapphire::Entity::BNpc::getLevelId() const
{
  return m_levelId;
}

bool Sapphire::Entity::BNpc::hasFlag( uint32_t flag ) const
{
  return m_flags & flag;
}

void Sapphire::Entity::BNpc::setFlag( uint32_t flag )
{
  m_flags |= flag;
}

void Sapphire::Entity::BNpc::autoAttack( CharaPtr pTarget )
{

  uint64_t tick = Util::getTimeMs();

  // todo: this needs to use the auto attack delay for the equipped weapon
  if( ( tick - m_lastAttack ) > 2500 )
  {
    pTarget->onActionHostile( getAsChara() );
    m_lastAttack = tick;
    srand( static_cast< uint32_t >( tick ) );

    auto pRNGMgr = m_pFw->get< World::Manager::RNGMgr >();
    auto damage = static_cast< uint16_t >( pRNGMgr->getRandGenerator< float >( m_level, m_level + m_level * 1.5f ).next() );

    auto effectPacket = std::make_shared< Server::EffectPacket >( getId(), pTarget->getId(), 7 );
    effectPacket->setRotation( Util::floatToUInt16Rot( getRot() ) );
    Common::EffectEntry effectEntry{};
    effectEntry.value = damage;
    effectEntry.effectType = ActionEffectType::Damage;
    effectEntry.hitSeverity = ActionHitSeverityType::NormalDamage;
    effectPacket->addEffect( effectEntry );

    sendToInRangeSet( effectPacket );

    pTarget->takeDamage( damage );

  }
}
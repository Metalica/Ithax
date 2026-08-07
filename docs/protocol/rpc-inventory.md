# machoNet RPC Inventory and Extension Boundary

> Version: 1.0.0
>
> Status: Verified against the approved server-side reference on 2026-08-07.
>
> Scope: the base machoNet RPC surface and the boundary for proposed
> Ithax relevancy extensions. The approved server-side reference is a
> separate AGPL-3.0 component; this document is a behavioral catalog, not
> copied code.

## 1. Dispatch Model

The approved server dispatches machoNet `CALL_REQ` packets to services by
name. Each service exposes methods as `Handle_<Method>` (or `<Method>`)
handlers. A call request carries:

- a remote object (an integer node id, or a string service name),
- a method name,
- a positional argument tuple,
- an optional named-argument dict.

The client-side equivalent is `macho.CallReq` with a substream payload
`[remoteObject, method, arg_tuple, arg_dict]` (see
`docs/protocol/machonet.md` section 4).

## 2. Verified Service Catalog

The approved server registers 201 service files across 70 service
categories with 1,690 `Handle_*` methods. The catalog below lists the
categories and the highest-value services for the base client flow. The
full per-method inventory is maintained in the knowledge base
(`stage4-rpc-inventory`); regenerate it from the approved server source
when the reference changes.

### 2.1 machoNet Core (post-handshake bootstrap)

| Service | Methods |
|---------|---------|
| `machoNet` | `GetInitVals`, `GetGlobalConfig`, `GetGlobalConfigValue`, `GetServerStatus`, `GetNodeID`, `GetNodeFromAddress`, `GetServiceInfo`, `GetTime`, `ForwardCharacterNotification`, `GetClusterGameStatisticsForClient`, `SetGlobalConfigValue`, `ReloadClientCodeHash` |
| `sessionMgr` | `GetSessionStatistics`, `RemoveSessionsFromServer` |
| `cache` | `GetServerVersionAndBuild` |
| `objCache` | `GetCachableObject`, `GetCachedObject`, `GetCachedObjectVersion`, `GetCachedMethodCallVersion`, `InvalidateCachedObject(s)`, `UpdateCache` |
| `ping` | `Ping` |

### 2.2 Login and Character Selection

| Service | Methods |
|---------|---------|
| `authentication` | `Login`, `Logout`, `AccruedTime`, `AmUnderage`, `SetLanguageID` |
| `char` | `GetCharactersToSelect`, `GetCharacterToSelect`, `GetCharacterSelectionData`, `SelectCharacterID`, `GetCharCreationInfo`, `ValidateNameEx`, `CreateCharacterWithDoll`, `DeleteCharacter`, `GetNumCharacters` |
| `charMgr` | `GetPublicInfo`, `GetPrivateInfo`, `GetCharacterDescription`, `GetHomeStation`, `GetContactList`, `GetSettingsInfo`, `GetCloneInfo`, `GetPaperdollState` |
| `user` | `GetUserName`, `GetUserToken`, `GetRedeemTokens`, `GetMultiCharactersTrainingSlots`, `UserLogOffCharacter` |

### 2.3 World Entry and Space

| Service | Methods |
|---------|---------|
| `beyonce` | `CmdDock`, `CmdUndock` (via `ship`), `CmdOrbit`, `CmdWarpToStuff`, `CmdSetSpeedFraction`, `CmdStop`, `CmdGotoBookmark`, `CmdAlignTo`, `CmdStargateJump`, `CmdJumpThroughFleet`, `UpdateStateRequest`, `MachoBindObject`, `MachoResolveObject` |
| `ship` | `ActivateShip`, `AssembleShip`, `Board`, `Undock`, `SafeLogoff`, `GetShipFittingInfo`, `GetModules`, `FitFitting`, `LaunchDrones`, `Scoop`, `Jettison` |
| `dogma` | `Activate`, `Deactivate`, `AddTarget`, `RemoveTarget`, `GetAllInfo`, `GetCharacterAttributes`, `GetTargets`, `LoadAmmo`, `UnloadAmmo`, `Overload`, `InjectSkillIntoBrain`, `MachoBindObject` |
| `destiny` | server-side movement authority; client sends `Cmd*` movement commands through `beyonce` |
| `scanMgr` | `ConeScan`, `GetFullState`, `RequestScans`, `RecoverProbes`, `DestroyProbe`, `SetProbeDestination` |
| `dungeon` | `GetInstancesForSolarsystem`, `GetCombatAnomalyInstances`, `GetSignatureInstances` |
| `map` | `GetStationInfo`, `GetSolarsystemItems`, `GetSecurityModifiedSystems`, `GetFacWarZoneInfo` |

### 2.4 Inventory, Fitting, Market, Chat, Mail

| Service | Methods |
|---------|---------|
| `invBroker` | `GetInventory`, `GetInventoryFromId`, `GetContainerContents`, `List`, `ListByFlags`, `ListDroneBay`, `ListFighterBay`, `ListFuelBay`, `Add`, `MultiAdd`, `MultiMerge`, `StackAll`, `TrashItems`, `FitFitting`, `StripFitting`, `DestroyFitting`, `AssembleCargoContainer`, `GetCapacity`, `MachoBindObject` |
| `charFitting` | `GetFittings`, `SaveFitting`, `DeleteFitting`, `UpdateFitting` |
| `market` | `GetOrders`, `GetNewPriceHistory`, `GetOldPriceHistory`, `GetMarketGroups`, `GetRegionBest`, `GetStationAsks`, `GetSystemAsks`, `StartupCheck` |
| `marketProxy` | `PlaceBuyOrder`, `PlaceMultiSellOrder`, `ModifyCharOrder`, `CancelCharOrder`, `GetCharOrders`, `GetCharEscrow`, `CharGetTransactions`, `GetHistoryForManyTypeIDs` |
| `LSC` | `GetChannels`, `JoinChannel`, `LeaveChannel`, `SendMessage`, `GetMyMessages` |
| `mail` | `GetMailHeaders`, `GetBody`, `SendMail`, `MarkAsRead`, `DeleteMail`, `GetLabels`, `AssignLabels` |
| `contracts` | `GetContractListForOwner`, `GetContract`, `CreateContract`, `AcceptContract`, `CompleteContract` |

### 2.5 Skills, Character, Corporation

| Service | Methods |
|---------|---------|
| `skillMgr` | `GetSkills`, `GetAllSkills`, `GetMySkillQueue`, `GetSkillQueue`, `SaveNewQueue`, `CharStartTrainingSkill`, `CharStopTrainingSkill`, `GetAttributes`, `GetImplants`, `GetBoosters`, `GetFreeSkillPoints`, `InjectSkillIntoBrain`, `MachoBindObject` |
| `corpRegistry` | `GetCorporation`, `GetMembers`, `GetMemberTrackingInfo`, `GetTitles`, `GetShareholders`, `GetBulletins`, `MachoBindObject` |
| `corpStationMgr` | `GetStationDetails`, `GetStationOffices`, `GetRentableItems`, `GetPotentialHomeStations` |
| `standingMgr` | `GetCharStandings`, `GetCorpStandings`, `GetStandingTransactions` |
| `insurance` | `GetContracts`, `GetInsurancePrices`, `InsureShip`, `GetItemsToInsure` |

### 2.6 Notifications and Session

| Service | Methods |
|---------|---------|
| `notificationMgr` | `GetAllNotifications`, `GetUnprocessed`, `MarkAsProcessed`, `DeleteNotifications` |
| `onlineStatus` | `GetInitialState`, `GetOnlineStatus`, `Prime` |
| `sessionMgr` | session-change notifications (`macho.SessionChangeNotification`) |

## 3. Base-Protocol Client Surface

For the Stage 4 base gate the client must support, at minimum:

1. `machoNet.GetInitVals` / `GetGlobalConfig` / `GetServerStatus` after the
   handshake (bootstrap).
2. `authentication.Login` and `char.GetCharactersToSelect` /
   `SelectCharacterID` (login flow).
3. `macho.CallReq` / `macho.CallRsp` round trips with typed dispatch.
4. `macho.PingReq` / `macho.PingRsp` heartbeat.
5. `macho.Notification` delivery to the main-owner command queue.
6. `macho.ErrorResponse` and `macho.TransportClosed` handling.

## 4. Extension Boundary (Proposed Relevancy Work)

The proposed Ithax relevancy messages are a **versioned server-side
extension** and are explicitly out of scope for the base-protocol gate:

- `EntityEnter`, `EntityLeave`, `EntityDelta`, `RelevancySnapshot` are
  placeholders until the external protocol extension is specified.
- They must not be confused with verified base machoNet messages.
- Extension negotiation, sequence/generation rules, resynchronization,
  forced relevance, byte budgets, and unknown-message behavior must be
  specified before implementation.

### 4.1 Unknown-Message Policy

- Unknown machoNet message types are rejected with a typed error.
- Unknown service methods are reported at the typed dispatch boundary and
  never silently dropped.
- Unknown notification types are delivered to the main-owner command queue
  with their raw payload for explicit handling.

## 5. Provenance

- Service catalog extracted from the approved server-side reference
  (`server/src/services/**`, `Handle_*` handlers) on 2026-08-07.
- Dispatch model verified against `server/src/services/baseService.js`.
- The full 1,690-method inventory is stored in the knowledge base
  (`stage4-rpc-inventory`) for regeneration and diffing.

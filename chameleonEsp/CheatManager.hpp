class CheatManager
{
public:
	struct PlayerInfo {
		std::string Name;
		SDK::FVector Location;
		SDK::AActor* Actor;
		bool IsSurvivor = false; // resolved on the game thread; lets the menu filter without touching a live UObject
	};

	// A render-ready snapshot of one player's ESP overlay, fully projected to screen space on the
	// game thread. The render thread only ever reads these - it never dereferences a live UObject,
	// which is what lets the draw pass run without racing the game thread's actor list.
	struct EspEntry {
		bool hasBox = false;
		SDK::FVector2D boxMin{};
		SDK::FVector2D boxMax{};
		std::vector<std::pair<SDK::FVector2D, SDK::FVector2D>> skeletonLines; // projected bone segments
		std::string name;
		int role = 0; // 0 = none, 1 = hunter, 2 = survivor
		float distanceMeters = 0.0f;
		bool hasSnapline = false;
		SDK::FVector2D snaplineScreen{};
		bool isVisible = false;
	};

	// Everything the render thread needs for one frame, produced wholesale on the game thread.
	struct EspSnapshot {
		std::vector<EspEntry> entries;
		std::vector<PlayerInfo> players; // backs the menu's teleport list
		bool magnetActive = false;
		float screenX = 0.0f;
		float screenY = 0.0f;
	};

private:
	SDK::UWorld** _UWorld;
	SDK::UWorld* gWorld;
	SDK::APlayerController* PlayerController;
	SDK::ULocalPlayer* LocalPlayer;
	SDK::UGameInstance* OwningGameInstance;
	SDK::UGameViewportClient* GameViewportClient;
	SDK::AGameStateBase* GameState;
	SDK::AActor* objActor;
	SDK::UGameplayStatics* UGStatics;
	SDK::UKismetSystemLibrary* KismetSystemLib;
	SDK::APawn* MyPlayer;
	SDK::ABP_FirstPersonCharacter_cLeon_Character_C* BaseClass; //change a class for each game
	SDK::UKismetMathLibrary* MathLib;
	int x, y = 0;

	// Resolve the world/player pointer chain into the members above. Returns false if any link is null.
	bool ResolveContext();
	// Per-player helpers, operating on the current `objActor`/`BaseClass` being iterated.
	std::string ResolvePlayerName();
	void UpdateForcedVisibility();
	bool IsDead();
	bool IsDead(SDK::AActor* actor);
	bool IsSurvivor();
	bool IsSurvivor(SDK::AActor* actor);
	bool IsSurvivor(SDK::ABP_FirstPersonCharacter_cLeon_Character_C* baseClass);
	bool IsHunter();
	bool IsHunter(SDK::AActor* actor);
	bool IsHunter(SDK::ABP_FirstPersonCharacter_cLeon_Character_C* baseClass);
	bool IsEnemy();

	// Game-thread builders: project the current actor's world state into a render-ready EspEntry.
	void BuildSkeletonLines(std::vector<std::pair<SDK::FVector2D, SDK::FVector2D>>& out);
	bool ComputeBoundingBox(SDK::FVector2D& BoxMin, SDK::FVector2D& BoxMax);
	void BuildEspEntry(EspEntry& entry, const std::string& PlayerName, SDK::FVector Location, SDK::FVector MyLocation, bool IsVisible);
	// Render-thread draw of a single prebuilt entry (ImGui only, no SDK/UObject access).
	void DrawEntry(const EspEntry& entry);

	void KillSurvivor(SDK::AActor* actor);
	void HandleTeleport(const std::unordered_set<SDK::AActor*>& currentActors);
	void HandleMagnet(const std::unordered_set<SDK::AActor*>& currentActors, const SDK::FVector& MyLocation, SDK::TArray<SDK::AActor*>& Players, EspSnapshot& snap);
	void HandleKillTarget(const std::unordered_set<SDK::AActor*>& currentActors);
	void HandleKillAllSurvivors(const std::unordered_set<SDK::AActor*>& currentActors);
	SDK::AActor* TeleportTarget = nullptr; // resolved by actor pointer, not list index, since the snapshot is rebuilt every frame
	SDK::AActor* KillTarget = nullptr;     // single-player kill request, resolved by actor pointer like TeleportTarget
	bool bKillAllSurvivorsRequested = false;

	// Actions that mutate game state (teleport, kill, force-visibility, magnet, etc.)
	// must run on the game thread; queue those actions and drain on the next ProcessEvent call.
	std::mutex GameThreadQueueMutex;
	std::vector<std::function<void()>> GameThreadQueue;

	// pendingSnapshot is written by the game thread (Init) and read by the render thread (RenderEsp)
	// under this mutex. drawSnapshot is the render thread's private working copy so it can draw
	// without holding the lock for the whole frame.
	std::mutex snapshotMutex;
	EspSnapshot pendingSnapshot;
	EspSnapshot drawSnapshot;
public:
	std::vector<PlayerInfo> PlayerInfos;
	void RequestTeleport(SDK::AActor* Actor) { TeleportTarget = Actor; }
	void RequestKillSurvivor(SDK::AActor* Actor) { KillTarget = Actor; }
	void RequestKillAllSurvivors() { bKillAllSurvivorsRequested = true; }
	std::unordered_set<SDK::AActor*> forcedVisibleActors;
	std::unordered_set<SDK::AActor*> deadActors; // actors seen ragdolling; latched so ESP stays off after the corpse stops simulating physics
	std::unordered_map<SDK::AActor*, std::string> playerNameCache; // last-known name per actor, so ESP survives PlayerState replication blips
	void Init();       // GAME THREAD: scan the world and publish a fresh snapshot
	void RenderEsp();  // RENDER THREAD: draw the latest published snapshot
	void DumpBones();
	void QueueGameThreadAction(std::function<void()> action);
	void FlushGameThreadActions();

	// True if Obj is still the live object at its GObjects slot. Queued actions capture raw
	// pointers on the render thread but only run later on the game thread (see
	// QueueGameThreadAction), so the actor may have been destroyed/GC'd in between - calling
	// into a freed UObject is exactly the null-pointer-deep-in-engine-code crash this guards.
	static bool IsObjectValid(SDK::UObject* Obj);
};

// Fill out your copyright notice in the Description page of Project Settings.


#include "PuzzlePlatformGameInstance.h"
#include "UObject/UObjectGlobals.h"
#include "Engine/Engine.h"
#include "OnlineSubsystem.h"
#include "OnlineSessionSettings.h"

#include "UObject/ConstructorHelpers.h"
#include "PlatformTrigger.h"
#include "Blueprint/UserWidget.h"
#include "MenuSystem/MainMenu.h"
#include "MenuSystem/MenuWidget.h"


const static FName SESSION_NAME = TEXT("GameSession");
const static FName SERVER_NAME_SETTINGS_KEY = TEXT("ServerName");

UPuzzlePlatformGameInstance::UPuzzlePlatformGameInstance(const FObjectInitializer& ObjectInitializer)
{
    ConstructorHelpers::FClassFinder<UUserWidget> MenuBPClass(TEXT("/Game/MenuSystem/WBP_MainMenu"));
    if(!ensure(MenuBPClass.Class != nullptr)) return;

    MenuClass = MenuBPClass.Class;
    
    ConstructorHelpers::FClassFinder<UUserWidget> InGameMenuBPClass(TEXT("/Game/MenuSystem/WBP_InGameMenu"));
    if(!ensure(InGameMenuBPClass.Class != nullptr)) return;

    InGameMenuClass = InGameMenuBPClass.Class;
}

void UPuzzlePlatformGameInstance::Init()
{
    IOnlineSubsystem* Subsystem = IOnlineSubsystem::Get();
    if(Subsystem != nullptr){
        UE_LOG(LogTemp, Warning, TEXT("Found Subsystem %s"), *Subsystem->GetSubsystemName().ToString());
        SessionInterface = Subsystem->GetSessionInterface();
        if(SessionInterface.IsValid()){
            SessionInterface->OnCreateSessionCompleteDelegates.AddUObject(this, &UPuzzlePlatformGameInstance::OnCreateSessionComplete);
            SessionInterface->OnDestroySessionCompleteDelegates.AddUObject(this, &UPuzzlePlatformGameInstance::OnDestroySessionComplete);
            SessionInterface->OnFindSessionsCompleteDelegates.AddUObject(this,&UPuzzlePlatformGameInstance::OnFindSessionComplete);
            SessionInterface->OnJoinSessionCompleteDelegates.AddUObject(this,&UPuzzlePlatformGameInstance::OnJoinSessionComplete);
        }
    }   
    else{
        UE_LOG(LogTemp, Warning, TEXT("Found no Subsystem"));
    }
    if(GEngine != nullptr){
        GEngine->OnNetworkFailure().AddUObject(this, &UPuzzlePlatformGameInstance::OnNetworkFailure);
    }
}

void UPuzzlePlatformGameInstance::Host(FString ServerName){
    
    DesiredServerName = ServerName;
    if(SessionInterface.IsValid()){
        FNamedOnlineSession* ExistingSession = SessionInterface->GetNamedSession(SESSION_NAME);
        if(ExistingSession != nullptr){
            SessionInterface->DestroySession(SESSION_NAME);
        }else{
            CreateSession();
        }

    }
}

void UPuzzlePlatformGameInstance::OnCreateSessionComplete(FName SessionName, bool Success)
{
    if(!Success){
        UE_LOG(LogTemp, Warning, TEXT("Could not create session"));
        return;
    }
    if (Menu != nullptr){
        Menu->OnLevelRemovedFromWorld(nullptr, GetWorld());
    }
    

    UEngine* Engine = GetEngine();
    if (!ensure(Engine != nullptr)) return;
    
    Engine->AddOnScreenDebugMessage(0, 2, FColor::Green, TEXT("Hosting"));

    UWorld* World = GetWorld();
    if (!ensure(World != nullptr)) return;

    World->ServerTravel("/Game/PuzzlePlatforms/Maps/Lobby?listen");
}
void UPuzzlePlatformGameInstance::OnDestroySessionComplete(FName SessionName, bool Success)
{
    if(Success){
        CreateSession();
    }
}

void UPuzzlePlatformGameInstance::OnNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType\
                            , const FString& ErrorString)
{
    LoadMainMenu();
}

void UPuzzlePlatformGameInstance::RefreshServerList()
{
    SessionSearch = MakeShareable(new FOnlineSessionSearch());
    if(SessionSearch.IsValid()){
        // SessionSearch->bIsLanQuery = true;
        SessionSearch->MaxSearchResults = 100;
        SessionSearch->QuerySettings.Set(SEARCH_PRESENCE, true, EOnlineComparisonOp::Equals);
        UE_LOG(LogTemp, Warning, TEXT("Starting to find sessions"));
        SessionInterface->FindSessions(0, SessionSearch.ToSharedRef());
    }
}

void UPuzzlePlatformGameInstance::OnFindSessionComplete(bool Success)
{
    if(Success && SessionSearch.IsValid() && Menu != nullptr){
        UE_LOG(LogTemp, Warning, TEXT("Finished finding sessions"));
        TArray<FServerData> ServerNames;
        // ServerNames.Add({"Test Server1"});
        // ServerNames.Add({"Test Server2"});
        // ServerNames.Add({"Test Server3"});
        for(const FOnlineSessionSearchResult& SearchResult:SessionSearch->SearchResults){
            UE_LOG(LogTemp, Warning, TEXT("Found session names: %s"), *SearchResult.GetSessionIdStr());
            FServerData Data;
            Data.MaxPlayers = SearchResult.Session.SessionSettings.NumPublicConnections;
            Data.CurrentPlayers = Data.MaxPlayers - SearchResult.Session.NumOpenPublicConnections;
            Data.HostUsername = SearchResult.Session.OwningUserName;
            
            FString ServerName;
            if (SearchResult.Session.SessionSettings.Get(SERVER_NAME_SETTINGS_KEY, ServerName)){
                Data.Name = ServerName;
            }
            else{
                Data.Name = "Could not find ServerName.";
            }
            ServerNames.Add(Data);
        }

        Menu->SetServerList(ServerNames);
    }
}

void UPuzzlePlatformGameInstance::CreateSession()
{
    if(SessionInterface.IsValid())
    {
        FOnlineSessionSettings SessionSettings;

        if (IOnlineSubsystem::Get()->GetSubsystemName() == "NULL"){
            SessionSettings.bIsLANMatch = true;

        }else{
            SessionSettings.bIsLANMatch = false;
        }        
        SessionSettings.NumPublicConnections = 5;
        SessionSettings.bShouldAdvertise = true;
        SessionSettings.bAllowInvites = true;
        SessionSettings.bUsesPresence = true;
        SessionSettings.bUseLobbiesIfAvailable = true;
        SessionSettings.Set(SERVER_NAME_SETTINGS_KEY, DesiredServerName, EOnlineDataAdvertisementType::Type::ViaOnlineServiceAndPing);

        SessionInterface->CreateSession(0, SESSION_NAME, SessionSettings);
    }

}
void UPuzzlePlatformGameInstance::Join(uint32 Index)
{   
    if(!SessionInterface.IsValid()) return;
    if(!SessionSearch.IsValid()) return;

    if (Menu != nullptr){
        // Menu->SetServerList({"Test1","Test2"});
        Menu->OnLevelRemovedFromWorld(nullptr, GetWorld());
    }

    SessionInterface->JoinSession(0, SESSION_NAME, SessionSearch->SearchResults[Index]);

}

void UPuzzlePlatformGameInstance::StartSession()
{
    if(SessionInterface.IsValid()){
        SessionInterface ->StartSession(SESSION_NAME);
    }
}

void UPuzzlePlatformGameInstance::OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
    if(!SessionInterface.IsValid()) return;

    FString Address;
    if(!SessionInterface->GetResolvedConnectString(SessionName, Address)){
        UE_LOG(LogTemp, Warning, TEXT("Could not get connect string."));
        return;
    }
    UEngine* Engine = GetEngine();
    if (!ensure(Engine != nullptr)) return;
    
    Engine->AddOnScreenDebugMessage(0, 5, FColor::Green, FString::Printf(TEXT("Joining %s"), *Address));
    
    APlayerController* PlayerController = GetFirstLocalPlayerController();
    if(!ensure(PlayerController != nullptr)) return;

    PlayerController->ClientTravel(Address, ETravelType::TRAVEL_Absolute);

}


void UPuzzlePlatformGameInstance::LoadMenu()
{
    if(!ensure(MenuClass != nullptr)) return;
    
    Menu = CreateWidget<UMainMenu>(this, MenuClass);
    if(!ensure(Menu != nullptr)) return;

    Menu->Setup();
    Menu->SetMenuInterface(this);
}

void UPuzzlePlatformGameInstance::InGameLoadMenu()
{
    if(!ensure(MenuClass != nullptr)) return;
    
    UMenuWidget* InGameMenu = CreateWidget<UMenuWidget>(this, InGameMenuClass);
    if(!ensure(InGameMenu != nullptr)) return;

    InGameMenu->Setup();
    InGameMenu->SetMenuInterface(this);
}

void UPuzzlePlatformGameInstance::LoadMainMenu()
{
    APlayerController* PlayerController = GetFirstLocalPlayerController();
    if(!ensure(PlayerController != nullptr)) return;

    PlayerController->ClientTravel("/Game/MenuSystem/MainMenu", ETravelType::TRAVEL_Absolute);
}




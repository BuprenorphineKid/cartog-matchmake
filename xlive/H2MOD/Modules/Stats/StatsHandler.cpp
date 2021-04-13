#include "StatsHandler.h"
#include "Util/hash.h"
#include "H2MOD\Modules\Utils\Utils.h"
#include "libcurl/curl/curl.h"
#include "rapidjson/writer.h"
#include "H2MOD/Modules/Startup/Startup.h"
#include "H2MOD/Modules/EventHandler/EventHandler.h"
#ifdef _DEBUG
#pragma comment(lib, "libcurl_a_debug.lib")
#else
#pragma comment(lib, "libcurl_a.lib")
#endif

static const bool verbose = true;
bool Registered = false;
bool MatchInvalidated = false;
StatsHandler::StatsAPIRegisteredStatus Status;

StatsHandler::StatsHandler()
{
	if (Memory::isDedicatedServer()) {
		Status.Registered = false;
		Status.RanksEnabled = false;
		Status.StatsEnabled = false;
		//Register callback on Post Game to upload the stats to the server
		EventHandler::registerGameStateCallback({
				"StatsSendStats",
				life_cycle_post_game,
				&this->sendStats
			}, true);
		//register callback on player leave to remove them from the packet filter
		EventHandler::registerNetworkPlayerRemoveCallback({
				"StatsPlayerLeave",
				this->playerLeftEvent
			}, true);
		//register callback on player join to send them their rank.
		EventHandler::registerNetworkPlayerAddCallback({
				"StatsPlayerJoin",
				this->playerJoinEvent
			}, true);
		//register a callback when the server reaches the lobby for the first time
		EventHandler::registerGameStateCallback({
				"InitStats",
				life_cycle_pre_game,
				[this]()
				{
					this->verifyRegistrationStatus();
					this->verifySendPlaylist();
					//Register callback to send player ranks on lobby and reset the match invalide state
					EventHandler::registerGameStateCallback({
						"StatsSendRanks",
						life_cycle_pre_game,
						[this]()
						{
							this->sendRankChange(true);
							this->InvalidateMatch(false);
						}
					}, true);
					EventHandler::registerGameStateCallback({
						"StatsSkipReset",
						life_cycle_in_game,
						[this]()
						{
							this->InvalidateMatch(false);
						}
					}, true);
				},
				true
			}, false);
		//Register a callback that will invalidate the current match if the skip command is used.
		EventHandler::registerServerCommandCallback({
				"StatsSkipPrevention",
				[this]() {this->InvalidateMatch(true);},
				ServerConsole::ServerConsoleCommands::skip
			}, true);
	} 
	else
	{
		//Register callback to reset rank to 255 on mainmenu
		EventHandler::registerGameStateCallback({
		"StatsResetRank",
		life_cycle_none,
		[]() { h2mod->set_local_rank(255); }
			}, false);
	}
}

/*
 * Player leave store the data  if non team game sort rank them at the bottom of the list
 * if team game rank them with their team, will require changes to the API. Will probably be easy
 * to impliment just modify the way placement is calculate to allow placements under 16
 * All placements under 16 should be considered maximum loss in non team games.
 * Team Games match with thier team
 * FFA place at losest remove maximum XP.
 */
wchar_t* StatsHandler::getPlaylistFile()
{
	auto output = Memory::GetAddress<wchar_t*>(0, 0x3B3704);
	return output;
}
std::string StatsHandler::getChecksum()
{
	std::string output;
	if (!hashes::calc_file_md5(getPlaylistFile(), output))
		output = "";
	return output;
}
struct curl_response_text {
	char *ptr;
	size_t len;
};

static void init_curl_response(struct curl_response_text *s) {
	s->len = 0;
	s->ptr = (char*)malloc(s->len + 1);
	if (s->ptr == NULL) {
		fprintf(stderr, "malloc() failed\n");
		exit(EXIT_FAILURE);
	}
	s->ptr[0] = '\0';
}

static size_t writefunc(void *ptr, size_t size, size_t nmemb, struct curl_response_text *s)
{
	size_t new_len = s->len + size * nmemb;
	s->ptr = (char*)realloc(s->ptr, new_len + 1);
	if (s->ptr == NULL) {
		fprintf(stderr, "realloc() failed\n");
		exit(EXIT_FAILURE);
	}
	memcpy(s->ptr + s->len, ptr, size*nmemb);
	s->ptr[new_len] = '\0';
	s->len = new_len;

	return size * nmemb;
}



void StatsHandler::verifyRegistrationStatus()
{
	auto checkResult = checkServerRegistration();
	if (checkResult != nullptr && strlen(checkResult) == 32)
	{
		//Check result returned a new AuthKey, attempt to register the server with it
		if (serverRegistration(checkResult)) {
			//Success save AuthKey to config
			LOG_TRACE_GAME("{} was successful.", __FUNCTION__);
			strncpy(H2Config_stats_authkey, checkResult, 32);
			free(checkResult);
			SaveH2Config();
			Status.Registered = true;
			Status.StatsEnabled = true;
		}
		else {
			LOG_ERROR_GAME("{} webserver error on register attempt, this will be attempted again next server launch", __FUNCTION__);
		}
	}
}

StatsHandler::StatsAPIRegisteredStatus StatsHandler::RegisteredStatus()
{
	return Status;
}


char* StatsHandler::checkServerRegistration()
{

	CURL *curl;
	CURLcode curlResult;
	CURLcode global_init = curl_global_init(CURL_GLOBAL_ALL);
	if (global_init != CURLE_OK)
	{
		LOG_ERROR_GAME("{} failed to init curl", __FUNCTION__);
		LOG_ERROR_GAME("curl_global_init(CURL_GLOBAL_ALL) failed: {}", curl_easy_strerror(global_init));
		return nullptr;
	}
	curl = curl_easy_init();
	if (curl)
	{
		std::string http_request_body = "https://www.halo2pc.com/test-pages/CartoStat/API/get.php?Type=ServerRegistrationCheck&Server_XUID=";
		auto sXUID = IntToString<::XUID>(NetworkSession::getCurrentNetworkSession()->membership.dedicated_server_xuid, std::dec);
		http_request_body += sXUID;
		struct curl_response_text s;
		init_curl_response(&s);


		//Set the URL for the GET
		curl_easy_setopt(curl, CURLOPT_URL, http_request_body.c_str());
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);

		curlResult = curl_easy_perform(curl);
		if (curlResult != CURLE_OK)
		{
			LOG_ERROR_GAME("{} failed to execute curl", __FUNCTION__);
		}
		else
		{
			int response_code;
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
			if(response_code == 500)
			{
				curl_easy_cleanup(curl);
				return nullptr; //Server Error
			}
			if (response_code == 200)
			{
				curl_easy_cleanup(curl);
				rapidjson::Document result;
				result.Parse(s.ptr);
				Status.Registered = true;
				Status.RanksEnabled = std::strncmp(result["RanksEnabled"].GetString(), "true", 4) == 0;
				Status.StatsEnabled = std::strncmp(result["StatsEnabled"].GetString(), "true", 4) == 0;
				return nullptr; //Server is registered
			}
			if(response_code == 201)
			{
				curl_easy_cleanup(curl);
				return s.ptr; //Server is unregisterd and returned a AuthKey
			}
			curl_easy_cleanup(curl);
		}
	}
	return nullptr; 
}

bool StatsHandler::serverRegistration(char* authKey)
{
	CURL *curl;
	CURLcode result;
	curl_mime *form = NULL;
	curl_mimepart *field = NULL;
	struct curl_slist *headerlist = NULL;
	static const char buf[] = "Expect:";
	curl_global_init(CURL_GLOBAL_ALL);
	curl = curl_easy_init();
	if (!curl)
	{
		LOG_ERROR_GAME("{} failed to init curl", __FUNCTION__);
		return false;
	}
	form = curl_mime_init(curl);
	field = curl_mime_addpart(form);
	curl_mime_name(field, "Type");
	curl_mime_data(field, "ServerRegistration", CURL_ZERO_TERMINATED);
	field = curl_mime_addpart(form);
	curl_mime_name(field, "Server_Name");
	curl_mime_data(field, H2Config_dedi_server_name, CURL_ZERO_TERMINATED);
	field = curl_mime_addpart(form);
	curl_mime_name(field, "Server_XUID");
	curl_mime_data(field, std::to_string(NetworkSession::getCurrentNetworkSession()->membership.dedicated_server_xuid).c_str(), CURL_ZERO_TERMINATED);
	field = curl_mime_addpart(form);
	curl_mime_name(field, "AuthKey");
	curl_mime_data(field, authKey, CURL_ZERO_TERMINATED);
	headerlist = curl_slist_append(headerlist, buf);

	curl_easy_setopt(curl, CURLOPT_URL, "https://www.halo2pc.com/test-pages/CartoStat/API/post.php");
	curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);
	result = curl_easy_perform(curl);
	if (result != CURLE_OK)
	{
		LOG_ERROR_GAME("{} failed to execute curl", __FUNCTION__);

		return false;
	}

	int response_code;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
	if(response_code == 500)
	{
		curl_easy_cleanup(curl);
		return false;
	}
	if(response_code == 200)
	{
		curl_easy_cleanup(curl);
		return true;
	}
	curl_easy_cleanup(curl);
	return response_code;

}

char* StatsHandler::getAPIToken()
{
	if (MatchInvalidated) {
		LOG_INFO_GAME("StatsHandler - Match was invalidated and will not be sent to the API");
		return "";
	}
	CURL *curl;
	CURLcode result;
	curl_mime *form = NULL;
	curl_mimepart *field = NULL;
	struct curl_slist *headerlist = NULL;
	static const char buf[] = "Expect:";
	curl_global_init(CURL_GLOBAL_ALL);
	curl = curl_easy_init();
	if (!curl)
	{
		LOG_ERROR_GAME("{} failed to init curl", __FUNCTION__);
		return "";
	}
	form = curl_mime_init(curl);
	field = curl_mime_addpart(form);
	curl_mime_name(field, "Type");
	curl_mime_data(field, "ServerLogin", CURL_ZERO_TERMINATED);
	field = curl_mime_addpart(form);
	curl_mime_name(field, "AuthKey");
	curl_mime_data(field, H2Config_stats_authkey, CURL_ZERO_TERMINATED);
	headerlist = curl_slist_append(headerlist, buf);
	
	struct curl_response_text s;
	init_curl_response(&s);
	curl_easy_setopt(curl, CURLOPT_URL, "https://www.halo2pc.com/test-pages/CartoStat/API/post.php");
	curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
	result = curl_easy_perform(curl);
	if (result != CURLE_OK)
	{
		LOG_ERROR_GAME("{} failed to execute curl", __FUNCTION__);
		curl_easy_cleanup(curl);
		return "";
	}

	int response_code;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
	if (response_code == 500)
	{
		LOG_INFO_GAME("{} failed get token for stats API", __FUNCTION__);
		curl_easy_cleanup(curl);
		return "";
	}
	if (response_code == 200)
	{
		curl_easy_cleanup(curl);
		return s.ptr;
	}
	curl_easy_cleanup(curl);
	return "";
}


int StatsHandler::uploadPlaylist(char* token)
{
	LOG_TRACE_GAME("Uploading Playlist");
	wchar_t* playlist_file = getPlaylistFile();
	char* pFile = (char*)malloc(sizeof(char) * wcslen(playlist_file) + 1);
	wcstombs2(pFile, playlist_file, wcslen(playlist_file) + 1);
	LOG_TRACE_GAME(pFile);
	std::string checksum = getChecksum();
	if (checksum.empty())
	{
		LOG_ERROR_GAME("{} failed to Hash Playlist file", __FUNCTION__);
		return -1;
	}
	LOG_TRACE_GAME(checksum);

	CURL *curl;
	CURLcode result;
	curl_mime *form = NULL;
	curl_mimepart *field = NULL;
	struct curl_slist *headerlist = NULL;
	curl_global_init(CURL_GLOBAL_ALL);
	curl = curl_easy_init();
	if(!curl)
	{
		LOG_ERROR_GAME("{} failed to init curl", __FUNCTION__);
		return -1;
	}
	form = curl_mime_init(curl);
	field = curl_mime_addpart(form);
	curl_mime_name(field, "file");
	curl_mime_filedata(field, pFile);
	field = curl_mime_addpart(form);
	curl_mime_name(field, "Type");
	curl_mime_data(field, "PlaylistUpload", CURL_ZERO_TERMINATED);
	field = curl_mime_addpart(form);
	curl_mime_name(field, "Playlist_Checksum");
	curl_mime_data(field, checksum.c_str(), CURL_ZERO_TERMINATED);
	field = curl_mime_addpart(form);
	curl_mime_name(field, "AuthToken");
	curl_mime_data(field, token, CURL_ZERO_TERMINATED);
	headerlist = curl_slist_append(headerlist, "Expect:");

	curl_easy_setopt(curl, CURLOPT_URL, "https://www.halo2pc.com/test-pages/CartoStat/API/post.php");
	curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);
	result = curl_easy_perform(curl);
	if(result != CURLE_OK)
	{
		LOG_ERROR_GAME("{} failed to execute curl", __FUNCTION__);
		//LOG_ERROR_GAME(curl_easy_strerror(result));
		return -1;
	}
	else
	{
		int response_code;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
		LOG_TRACE_GAME("{} response code: {}", __FUNCTION__, std::to_string(response_code));
		return response_code;
	}
	return -1;
}

int StatsHandler::verifyPlaylist(char* token)
{
	LOG_TRACE_GAME("Verifying Playlist..");
	std::string http_request_body = "https://www.halo2pc.com/test-pages/CartoStat/API/get.php?Type=PlaylistCheck&Playlist_Checksum=";
	wchar_t* playlist_file = getPlaylistFile();
	LOG_TRACE_GAME("{} loaded playlist is: ", __FUNCTION__);
	LOG_TRACE_GAME(L"{}", playlist_file);

	std::string checksum = getChecksum();
	if (checksum == "")
	{
		LOG_ERROR_GAME("{} failed to Hash Playlist file", __FUNCTION__);
		return -1;
	}
	else
	{
		LOG_TRACE_GAME(checksum);	
		http_request_body.append(checksum);
		LOG_TRACE_GAME(http_request_body);
		http_request_body.append("&AuthToken=");
		http_request_body.append(token);
		char* Response;
		CURL *curl;
		CURLcode curlResult;
		CURLcode global_init = curl_global_init(CURL_GLOBAL_ALL);
		if(global_init != CURLE_OK)
		{
			LOG_INFO_GAME("{} failed to init curl", __FUNCTION__);
			return -1;
		}
		curl = curl_easy_init();
		if(curl)
		{
			//Set the URL for the GET
			curl_easy_setopt(curl, CURLOPT_URL, http_request_body.c_str());
			curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
			curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
			curlResult = curl_easy_perform(curl);
			if(curlResult != CURLE_OK)
			{
				LOG_ERROR_GAME("{} failed to execute curl", __FUNCTION__);
				LOG_ERROR_GAME("curl_global_init(CURL_GLOBAL_ALL) failed: {}", curl_easy_strerror(global_init));
				curl_easy_cleanup(curl);
				return -1;
			} else
			{
				int response_code;
				curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
				LOG_TRACE_GAME("{} response code: {}", __FUNCTION__, std::to_string(response_code));
				curl_easy_cleanup(curl);
				return response_code;
			}
			curl_easy_cleanup(curl);
		}
	}

	return -1;
}

int StatsHandler::uploadStats(char* filepath, char* token)
{
	CURL *curl;
	CURLcode result;
	curl_mime *form = NULL;
	curl_mimepart *field = NULL;
	struct curl_slist *headerlist = NULL;
	static const char buf[] = "Expect:";
	curl_global_init(CURL_GLOBAL_ALL);
	curl = curl_easy_init();
	if (!curl)
	{
		LOG_ERROR_GAME("{} failed to init curl", __FUNCTION__);
		return -1;
	}
	form = curl_mime_init(curl);
	field = curl_mime_addpart(form);
	curl_mime_name(field, "file");
	curl_mime_filedata(field, filepath);
	field = curl_mime_addpart(form);
	curl_mime_name(field, "Type");
	curl_mime_data(field, "GameStats", CURL_ZERO_TERMINATED);
	field = curl_mime_addpart(form);
	curl_mime_name(field, "AuthToken");
	curl_mime_data(field, token, CURL_ZERO_TERMINATED);
	headerlist = curl_slist_append(headerlist, buf);

	curl_easy_setopt(curl, CURLOPT_URL, "https://www.halo2pc.com/test-pages/CartoStat/API/post.php");
	curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);
	result = curl_easy_perform(curl);
	if (result != CURLE_OK)
	{
		LOG_ERROR_GAME("{} failed to execute curl", __FUNCTION__);
		//LOG_ERROR_GAME(curl_easy_strerror(result));
		curl_easy_cleanup(curl);
		return -1;
	}
	else
	{
		int response_code;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
		curl_easy_cleanup(curl);
		return response_code;
	}
	return -1;
}

//Really should make structs for these but I don't want to take the time to learn how.
static const int baseOffset  = 0x4dc722;
static const int rtPCROffset = 0x4DD1EE;
static const int PCROffset   = 0x49F6B0;

char* StatsHandler::buildJSON()
{

	WDocument document;
	WValue value;
	document.SetObject();


	//Inital Properties
	rapidjson::Document::AllocatorType& allocator = document.GetAllocator();
	auto checksum = getChecksum();
	auto wchecksum = std::wstring(checksum.begin(), checksum.end());
	value.SetString(wchecksum.c_str(), allocator);
	document.AddMember(L"PlaylistChecksum", value, allocator);
	auto Scenario = Memory::GetAddress<wchar_t*>(0, 0x4DC504);

	std::wstring ScenarioPath = std::wstring(Scenario);
	std::wstring Scen(ScenarioPath.substr(ScenarioPath.rfind('\\') + 1));

	value.SetString(Scen.c_str(), allocator);
	document.AddMember(L"Scenario", value, allocator);

	//Build the Variant Object
	WValue Variant(rapidjson::kObjectType);
	auto VariantName = Memory::GetAddress<wchar_t*>(0, 0x4DC3D4);
	value.SetString(VariantName, allocator);
	Variant.AddMember(L"Name", value, allocator);
	BYTE VariantType = *Memory::GetAddress<BYTE*>(0, 0x4dc414);
	Variant.AddMember(L"Type", VariantType, allocator);
	
	WValue VariantSettings(rapidjson::kObjectType);
	BYTE TeamPlay = *Memory::GetAddress<BYTE*>(0, 0x992880);
	VariantSettings.AddMember(L"Team Play", TeamPlay, allocator);
	Variant.AddMember(L"Settings", VariantSettings, allocator);
	document.AddMember(L"Variant", Variant, allocator);
	
	auto ServerName = Memory::GetAddress<wchar_t*>(0, 0x52FC88);

	//Players
	int playerCount = 0;
	WValue Players(rapidjson::kArrayType);
	for(auto i = 0; i < 16; i++)
	{
		int calcBaseOffset  = baseOffset + (i * 0x94);
		auto XUID = *Memory::GetAddress<::XUID*>(0, calcBaseOffset);
		if (XUID == 0) //Skip if it doesnt exists
			continue;


		int calcRTPCROffset = rtPCROffset + (i * 0x36A);
		int calcPCROffset = 0;
		int EndgameIndex = 0;
		WValue Player(rapidjson::kObjectType);
		
		
		auto Gamertag = Memory::GetAddress<wchar_t*>(0, calcBaseOffset + 0xA);
		for(auto j = 0; j < 16; j++)
		{
			auto tGamertag = Memory::GetAddress<wchar_t*>(0, PCROffset + (j * 0x110));
			//I dont know why but I couldn't get any other method of comparing to work,
			//Someone else can fix it because I know this probably is a terrible way
			//To compare the wchar_t*.
			if(std::wstring(Gamertag) == std::wstring(tGamertag))
			{
				calcPCROffset = PCROffset + (j * 0x110);
				EndgameIndex = i;
				break;
			}
		}
		
		playerCount++;

		#pragma region Memory_Reading
		auto PrimaryColor = *Memory::GetAddress<BYTE*>(0, calcBaseOffset + 0x4A);
		auto SecondaryColor = *Memory::GetAddress<BYTE*>(0, calcBaseOffset + 0x4B);
		auto PrimaryEmblem = *Memory::GetAddress<BYTE*>(0, calcBaseOffset + 0x4C);
		auto SecondaryEmblem = *Memory::GetAddress<BYTE*>(0, calcBaseOffset + 0x4D);
		auto PlayerModel = *Memory::GetAddress<BYTE*>(0, calcBaseOffset + 0x4E);
		auto EmblemForeground = *Memory::GetAddress<BYTE*>(0, calcBaseOffset + 0x4F);
		auto EmblemBackground = *Memory::GetAddress<BYTE*>(0, calcBaseOffset + 0x50);
		auto EmblemToggle = *Memory::GetAddress<BYTE*>(0, calcBaseOffset + 0x51);
		auto ClanDescripton = Memory::GetAddress<wchar_t*>(0, calcBaseOffset + 0x5A);
		auto ClanTag = Memory::GetAddress<wchar_t*>(0, calcBaseOffset + 0x7A);
		auto Team = *Memory::GetAddress<BYTE*>(0, calcBaseOffset + 0x86);
		auto Handicap = *Memory::GetAddress<BYTE*>(0, calcBaseOffset + 0x87);
		auto Rank = *Memory::GetAddress<BYTE*>(0, calcBaseOffset + 0x88);
		auto Nameplate = *Memory::GetAddress<BYTE*>(0, calcBaseOffset + 0x8B);
		
		wchar_t Place[16];
		std::wcsncpy(Place, Memory::GetAddress<wchar_t*>(0, calcPCROffset + 0xE0), 16);
		
		wchar_t Score[16];
		std::wcsncpy(Score, Memory::GetAddress<wchar_t*>(0, calcPCROffset + 0x40), 16);

		auto Kills = *Memory::GetAddress<unsigned short*>(0, calcRTPCROffset);
		auto Assists = *Memory::GetAddress<unsigned short*>(0, calcRTPCROffset + 0x2);
		auto Deaths = *Memory::GetAddress<unsigned short*>(0, calcRTPCROffset + 0x4);
		auto Betrayals = *Memory::GetAddress<unsigned short*>(0, calcRTPCROffset + 0x6);
		auto Suicides = *Memory::GetAddress<unsigned short*>(0, calcRTPCROffset + 0x8);
		auto BestSpree = *Memory::GetAddress<unsigned short*>(0, calcRTPCROffset + 0xA);
		auto TimeAlive = *Memory::GetAddress<unsigned short*>(0, calcRTPCROffset + 0xC);
		auto ShotsFired = *Memory::GetAddress<unsigned short*>(0, calcPCROffset + 0x84);
		auto ShotsHit = *Memory::GetAddress<unsigned short*>(0, calcPCROffset + 0x88);
		auto HeadShots = *Memory::GetAddress<unsigned short*>(0, calcPCROffset + 0x8C);

		auto FlagScores = *Memory::GetAddress<unsigned short*>(0, calcRTPCROffset + 0xE);
		auto FlagSteals = *Memory::GetAddress<unsigned short*>(0, calcRTPCROffset + 0x10);
		auto FlagSaves = *Memory::GetAddress<unsigned short*>(0, calcRTPCROffset + 0x12);
		auto FlagUnk = *Memory::GetAddress<unsigned short*>(0, calcRTPCROffset + 0x14);

		auto BombScores = *Memory::GetAddress<unsigned short*>(0, calcRTPCROffset + 0x18);
		auto BombKills = *Memory::GetAddress<unsigned short*>(0, calcRTPCROffset + 0x1A);
		auto BombGrabs = *Memory::GetAddress<unsigned short*>(0, calcRTPCROffset + 0x1C);

		auto BallScore = *Memory::GetAddress<unsigned short*>(0, calcRTPCROffset + 0x20);
		auto BallKills = *Memory::GetAddress<unsigned short*>(0, calcRTPCROffset + 0x22);
		auto BallCarrierKills = *Memory::GetAddress<unsigned short*>(0, calcRTPCROffset + 0x24);

		auto KingKillsAsKing = *Memory::GetAddress<unsigned short*>(0, calcRTPCROffset + 0x26);
		auto KingKilledKings = *Memory::GetAddress<unsigned short*>(0, calcRTPCROffset + 0x28);
		
		auto JuggKilledJuggs = *Memory::GetAddress<unsigned short*>(0, calcRTPCROffset + 0x3C);
		auto JuggKillsAsJugg = *Memory::GetAddress<unsigned short*>(0, calcRTPCROffset + 0x3E);
		auto JuggTime = *Memory::GetAddress<unsigned short*>(0, calcRTPCROffset + 0x40);

		auto TerrTaken = *Memory::GetAddress<unsigned short*>(0, calcRTPCROffset + 0x48);
		auto TerrLost = *Memory::GetAddress<unsigned short*>(0, calcRTPCROffset + 0x46);
		#pragma endregion 

		#pragma region Document_Writing
		value.SetInt(EndgameIndex);
		Player.AddMember(L"EndGameIndex", value, allocator);
		value.SetString(IntToWString<::XUID>(XUID, std::dec).c_str(), allocator);
		Player.AddMember(L"XUID", value, allocator);
		value.SetString(Gamertag, allocator);
		Player.AddMember(L"Gamertag", value, allocator);
		Player.AddMember(L"PrimaryColor", PrimaryColor, allocator);
		Player.AddMember(L"SecondaryColor", SecondaryColor, allocator);
		Player.AddMember(L"PrimaryEmblem", PrimaryEmblem, allocator);
		Player.AddMember(L"SecondaryEmblem", SecondaryEmblem, allocator);
		Player.AddMember(L"PlayerModel", PlayerModel, allocator);
		Player.AddMember(L"EmblemForeground", EmblemForeground, allocator);
		Player.AddMember(L"EmblemBackground", EmblemBackground, allocator);
		Player.AddMember(L"EmblemToggle", EmblemToggle, allocator);
		value.SetString(ClanDescripton, allocator);
		Player.AddMember(L"ClanDescription", value, allocator);
		value.SetString(ClanTag, allocator);
		Player.AddMember(L"ClanTag", value, allocator);
		Player.AddMember(L"Team", Team, allocator);
		Player.AddMember(L"Handicap", Handicap, allocator);
		Player.AddMember(L"Rank", Rank, allocator);
		Player.AddMember(L"Nameplate", Nameplate, allocator);
		value.SetString(Place, allocator);
		Player.AddMember(L"Place", value, allocator);
		value.SetString(Score, allocator);
		Player.AddMember(L"Score", value, allocator);
		value.SetInt(Kills);
		Player.AddMember(L"Kills", Kills, allocator);
		value.SetInt(Assists);
		Player.AddMember(L"Assists", value, allocator);
		value.SetInt(Deaths);
		Player.AddMember(L"Deaths", value, allocator);
		value.SetInt(Betrayals);
		Player.AddMember(L"Betrayals", value, allocator);
		value.SetInt(Suicides);
		Player.AddMember(L"Suicides", value, allocator);
		value.SetInt(BestSpree);
		Player.AddMember(L"BestSpree", value, allocator);
		value.SetInt(TimeAlive);
		Player.AddMember(L"TimeAlive", value, allocator);
		value.SetInt(ShotsFired);
		Player.AddMember(L"ShotsFired", value, allocator);
		value.SetInt(ShotsHit);
		Player.AddMember(L"ShotsHit", value, allocator);
		value.SetInt(HeadShots);
		Player.AddMember(L"HeadShots", value, allocator);
		value.SetInt(FlagScores);
		Player.AddMember(L"FlagScores", value, allocator);
		value.SetInt(FlagSteals);
		Player.AddMember(L"FlagSteals", value, allocator);
		value.SetInt(FlagSaves);
		Player.AddMember(L"FlagSaves", value, allocator);
		value.SetInt(FlagUnk);
		Player.AddMember(L"FlagUnk", value, allocator);
		value.SetInt(BombScores);
		Player.AddMember(L"BombScores", value, allocator);
		value.SetInt(BombKills);
		Player.AddMember(L"BombKills", value, allocator);
		value.SetInt(BombGrabs);
		Player.AddMember(L"BombGrabs", value, allocator);
		value.SetInt(BallScore);
		Player.AddMember(L"BallScore", value, allocator);
		value.SetInt(BallKills);
		Player.AddMember(L"BallKills", value, allocator);
		value.SetInt(BallCarrierKills);
		Player.AddMember(L"BallCarrierKills", value, allocator);
		value.SetInt(KingKillsAsKing);
		Player.AddMember(L"KingKillsAsKing", value, allocator);
		value.SetInt(KingKilledKings);
		Player.AddMember(L"KingKilledKings", value, allocator);
		value.SetInt(JuggKilledJuggs);
		Player.AddMember(L"JuggKilledJuggs", value, allocator);
		value.SetInt(JuggKillsAsJugg);
		Player.AddMember(L"JuggKillsAsJugg", value, allocator);
		value.SetInt(JuggTime);
		Player.AddMember(L"JuggTime", value, allocator);
		value.SetInt(TerrTaken);
		Player.AddMember(L"TerrTaken", value, allocator);
		value.SetInt(TerrLost);
		Player.AddMember(L"TerrLost", value, allocator);
		
		WValue Medals(rapidjson::kArrayType);
		for(auto j = 0; j < 24; j++)
		{
			value.SetInt(*Memory::GetAddress<unsigned short*>(0, calcRTPCROffset + 0x4A + (j * 2)));
			Medals.PushBack(value, allocator);
		}
		Player.AddMember(L"MedalData", Medals, allocator);

		WValue DamageReport(rapidjson::kObjectType);
		for(int j = 0; j < 36; j++)
		{
			WValue DamageType(rapidjson::kArrayType);
			int uselessCheck = 0;
			for(auto k = 0; k < 7; k++)
			{
				/*
				 * 0 = Kills
				 * 1 = Deaths
				 * 2 = Betrayals
				 * 3 = Suicides
				 * 4 = Shots Fired
				 * 5 = Shots Hit
				 * 6 = Headshot kills
				 * 8 = Nothing stored here, move on.
				 */
				auto val = *Memory::GetAddress<unsigned short*>(0, calcRTPCROffset + 0x8E + (j * 0x10) + (k * 2));
				value.SetInt(val);
				DamageType.PushBack(value, allocator);
				if (val > 0)
					uselessCheck++;
			}
			if (uselessCheck > 0) {
				WValue n(std::to_wstring(j).c_str(), allocator); //This is fucking stupid.
				DamageReport.AddMember(n, DamageType, allocator);
			}
		}
		Player.AddMember(L"DamageData", DamageReport, allocator);
		#pragma endregion

		Players.PushBack(Player, allocator);
	}


	for (auto i = 0; i < playerCount; i++) {
		WValue Versus(rapidjson::kArrayType);
		int iOffset = PCROffset + Players[i][L"EndGameIndex"].GetInt() * 0x110;
		for (auto j = 0; j < playerCount; j++)
		{
			WValue Matchup(rapidjson::kArrayType);
			int jOffset = PCROffset + Players[j][L"EndGameIndex"].GetInt() * 0x110;
			//Player I: Killed J
			value.SetInt(*Memory::GetAddress<unsigned int*>(0, iOffset + 0x90 + (j * 0x4)));
			Matchup.PushBack(value, allocator);
			//Player I: Killed by J
			value.SetInt(*Memory::GetAddress<unsigned int*>(0, jOffset + 0x90 + (i * 0x4)));
			Matchup.PushBack(value, allocator);
			Versus.PushBack(Matchup, allocator);
		}
		Players[i].AddMember(L"VersusData", Versus, allocator);
	}

	document.AddMember(L"Players", Players, allocator);


	//Export the file
	rapidjson::StringBuffer strbuf;
	rapidjson::Writer<rapidjson::StringBuffer, rapidjson::UTF16<>> writer(strbuf);
	document.Accept(writer);
	std::string json(strbuf.GetString(), strbuf.GetSize());

	wchar_t fileOutPath[1024];
	time_t timer;
	struct tm y2k = { 0 };
	double seconds;

	y2k.tm_hour = 0;   y2k.tm_min = 0; y2k.tm_sec = 0;
	y2k.tm_year = 100; y2k.tm_mon = 0; y2k.tm_mday = 1;

	time(&timer);
	seconds = difftime(timer, mktime(&y2k));
	wchar_t unix[100];

	swprintf(unix, 100, L"%.f", seconds);
	auto sXUID = IntToWString<::XUID>(NetworkSession::getCurrentNetworkSession()->membership.dedicated_server_xuid, std::dec);
	swprintf(fileOutPath, 1024, L"%wsz%s-%s.json", H2ProcessFilePath, sXUID.c_str(), unix);
	std::ofstream of(fileOutPath);
	of << json;
	if (!of.good())
	{
		LOG_ERROR_GAME("{} Failed to write to file", __FUNCTION__);
	}
	char* oFile = (char*)malloc(sizeof(char) * wcslen(fileOutPath) + 1);
	wcstombs2(oFile, fileOutPath, wcslen(fileOutPath) + 1);
	return oFile;
}

std::vector<XUID> alreadySent;

void StatsHandler::InvalidateMatch(bool state)
{
	MatchInvalidated = state;
}

void StatsHandler::verifyPlayerRanks()
{
	if(Status.RanksEnabled)
		for (auto i = 0; i < NetworkSession::getPlayerCount(); i++)
			//if(ServerStatus.Ranked)
			if (NetworkSession::getPlayerInformation(i)->properties.player_displayed_skill >= 50
				&& !std::none_of(alreadySent.begin(), alreadySent.end(), [&](XUID xuid) -> bool { return xuid == NetworkSession::getPlayerInformation(i)->identifier; }))
					sendRankChange(true);
}
void StatsHandler::playerLeftEvent(int peerIndex)
{
	//LeftPlayers.push_back(XUID)
	auto it = std::find(alreadySent.begin(), alreadySent.end(), NetworkSession::getPeerXUID(peerIndex));
	if(it != alreadySent.end())
		alreadySent.erase(it);
}

void StatsHandler::playerJoinEvent(int peerIndex)
{
	/*No need to do anything fancy like filtering or 
	 *another function to send a specific rank
	 *The main function already filters out players
	 *who have been sent their rank before so just fire
	 *the main function.*/
	
	sendRankChange();
}

rapidjson::Document StatsHandler::getPlayerRanks(bool forceAll)
{
	std::string XUIDs;
	rapidjson::Document document;
	BYTE playerCount = 0;
	if (forceAll)
		alreadySent.clear();
	LOG_TRACE_GAME(L"Fetching player rank(s) - Total Peers {}", NetworkSession::getPeerCount() - 1);
	for (auto i = 0; i < NetworkSession::getPeerCount(); i++)
	{
		bool addSeparator = false;
		if(NetworkSession::getLocalPeerIndex() != i)
		{
			auto peerXUID = NetworkSession::getPeerXUID(i);
			if(peerXUID == NONE)
			{
				LOG_TRACE_GAME(L"No XUID found for Peer: {}", i);
			} 
			else
				if (std::none_of(alreadySent.begin(), alreadySent.end(), [&](XUID xuid) -> bool { return xuid == peerXUID; }))
				{
					LOG_TRACE_GAME(NetworkSession::getPeerPlayerName(i));
					LOG_TRACE_GAME(L"\t Peer: {} XUID: {}", i, IntToWString(peerXUID, std::dec));
					playerCount++;
					XUIDs.append(IntToString(peerXUID, std::dec));
					alreadySent.push_back(peerXUID);
					addSeparator = true;
				} else
				{
					LOG_TRACE_GAME(L"{0} - Skipped", NetworkSession::getPeerPlayerName(i));
					LOG_TRACE_GAME(L"\t Peer: {0} XUID: {1} Name: {2}", i, IntToWString(peerXUID, std::dec));
				}
		}

		// check if we should separate, also do not add the last separator if next iteration will break out of the loop
		if (addSeparator && i + 1 < NetworkSession::getPeerCount())
			XUIDs.append(",");
	}
	if (playerCount == 0) return document;
	std::string http_request_body = "https://www.halo2pc.com/test-pages/CartoStat/API/get.php?Type=PlaylistRanks&Playlist_Checksum=";
	http_request_body.append(getChecksum());
	http_request_body.append("&Server_XUID=");
	http_request_body.append(std::to_string(NetworkSession::getCurrentNetworkSession()->membership.dedicated_server_xuid));
	http_request_body.append("&Player_XUIDS=");
	http_request_body.append(XUIDs);
	
	CURL *curl;
	CURLcode curlResult;
	CURLcode global_init = curl_global_init(CURL_GLOBAL_ALL);
	if (global_init != CURLE_OK)
	{
		LOG_ERROR_GAME("{} failed to init curl", __FUNCTION__);
		LOG_ERROR_GAME("curl_global_init(CURL_GLOBAL_ALL) failed: {}", curl_easy_strerror(global_init));
		alreadySent.clear();
		return document;
	}
	curl = curl_easy_init();
	if (curl)
	{

		struct curl_response_text s;
		init_curl_response(&s);


		//Set the URL for the GET
		curl_easy_setopt(curl, CURLOPT_URL, http_request_body.c_str());
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);

		curlResult = curl_easy_perform(curl);
		if (curlResult != CURLE_OK)
		{
			LOG_ERROR_GAME("{} - failed to init/execute curl!", __FUNCTION__);
			LOG_ERROR_GAME("curl_global_init(CURL_GLOBAL_ALL) failed: {}", curl_easy_strerror(global_init));
			curl_easy_cleanup(curl);
			return document;
		}
		else
		{

			int response_code;
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
			LOG_TRACE_GAME(s.ptr);
			document.Parse(s.ptr);
			curl_easy_cleanup(curl);
			free(s.ptr);
			return document;
		}
		
	}
	return document;
}

void StatsHandler::init()
{

}

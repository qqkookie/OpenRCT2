/*****************************************************************************
 * Copyright (c) 2014-2018 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "ScenarioRepository.h"

#include "../Context.h"
#include "../Game.h"
#include "../ParkImporter.h"
#include "../PlatformEnvironment.h"
#include "../config/Config.h"
#include "../core/Console.hpp"
#include "../core/File.h"
#include "../core/FileIndex.hpp"
#include "../core/FileStream.hpp"
#include "../core/Math.hpp"
#include "../core/Path.hpp"
#include "../core/String.hpp"
#include "../core/Util.hpp"
#include "../localisation/Language.h"
#include "../localisation/Localisation.h"
#include "../localisation/LocalisationService.h"
#include "../platform/platform.h"
#include "../rct12/SawyerChunkReader.h"
#include "../config/IniReader.hpp"
#include "../config/IniWriter.hpp"
#include "Scenario.h"
#include "ScenarioSources.h"

#include <algorithm>
#include <memory>
#include <vector>

using namespace OpenRCT2;

static int32_t ScenarioCategoryCompare(int32_t categoryA, int32_t categoryB)
{
    if (categoryA == categoryB)
        return 0;
    if (categoryA == SCENARIO_CATEGORY_DLC)
        return -1;
    if (categoryB == SCENARIO_CATEGORY_DLC)
        return 1;
    if (categoryA == SCENARIO_CATEGORY_BUILD_YOUR_OWN)
        return -1;
    if (categoryB == SCENARIO_CATEGORY_BUILD_YOUR_OWN)
        return 1;
    return Math::Sign(categoryA - categoryB);
}

static int32_t scenario_index_entry_CompareByCategory(const scenario_index_entry& entryA, const scenario_index_entry& entryB)
{
    // Order by category
    if (entryA.category != entryB.category)
    {
        return ScenarioCategoryCompare(entryA.category, entryB.category);
    }

    // Then by source game / name
    switch (entryA.category)
    {
        default:
            if (entryA.source_game != entryB.source_game)
            {
                return entryA.source_game - entryB.source_game;
            }
            return strcmp(entryA.name, entryB.name);
        case SCENARIO_CATEGORY_REAL:
        case SCENARIO_CATEGORY_OTHER:
            return strcmp(entryA.name, entryB.name);
    }
}

static int32_t scenario_index_entry_CompareByIndex(const scenario_index_entry& entryA, const scenario_index_entry& entryB)
{
    // Order by source game
    if (entryA.source_game != entryB.source_game)
    {
        return entryA.source_game - entryB.source_game;
    }

    // Then by index / category / name
    uint8_t sourceGame = entryA.source_game;
    switch (sourceGame)
    {
        default:
            if (entryA.source_index == -1 && entryB.source_index == -1)
            {
                if (entryA.category == entryB.category)
                {
                    return scenario_index_entry_CompareByCategory(entryA, entryB);
                }
                else
                {
                    return ScenarioCategoryCompare(entryA.category, entryB.category);
                }
            }
            else if (entryA.source_index == -1)
            {
                return 1;
            }
            else if (entryB.source_index == -1)
            {
                return -1;
            }
            else
            {
                return entryA.source_index - entryB.source_index;
            }
        case SCENARIO_SOURCE_REAL:
            return scenario_index_entry_CompareByCategory(entryA, entryB);
    }
}

static void scenario_highscore_free(scenario_highscore_entry* highscore)
{
    SafeFree(highscore->scen_file);
    SafeFree(highscore->scen_winner);
    SafeDelete(highscore);
}

class ScenarioFileIndex final : public FileIndex<scenario_index_entry>
{
private:
    static constexpr uint32_t MAGIC_NUMBER = 0x58444953; // SIDX
    static constexpr uint16_t VERSION = 3;
    static constexpr auto PATTERN = "*.sc4;*.sc6";

public:
    explicit ScenarioFileIndex(const IPlatformEnvironment& env)
        : FileIndex(
              "scenario index", MAGIC_NUMBER, VERSION, env.GetFilePath(PATHID::CACHE_SCENARIOS), std::string(PATTERN),
              std::vector<std::string>({
                  env.GetDirectoryPath(DIRBASE::RCT1, DIRID::SCENARIO),
                  env.GetDirectoryPath(DIRBASE::RCT2, DIRID::SCENARIO),
                  env.GetDirectoryPath(DIRBASE::USER, DIRID::SCENARIO),
              }))
    {
    }

protected:
    std::tuple<bool, scenario_index_entry> Create(int32_t, const std::string& path) const override
    {
        scenario_index_entry entry;
        auto timestamp = File::GetLastModified(path);
        if (GetScenarioInfo(path, timestamp, &entry))
        {
            return std::make_tuple(true, entry);
        }
        else
        {
            return std::make_tuple(true, scenario_index_entry());
        }
    }

    void Serialise(IStream* stream, const scenario_index_entry& item) const override
    {
        stream->Write(item.path, sizeof(item.path));
        stream->WriteValue(item.timestamp);

        stream->WriteValue(item.category);
        stream->WriteValue(item.source_game);
        stream->WriteValue(item.source_index);
        stream->WriteValue(item.sc_id);

        stream->WriteValue(item.objective_type);
        stream->WriteValue(item.objective_arg_1);
        stream->WriteValue(item.objective_arg_2);
        stream->WriteValue(item.objective_arg_3);

        stream->Write(item.internal_name, sizeof(item.internal_name));
        stream->Write(item.name, sizeof(item.name));
        stream->Write(item.details, sizeof(item.details));
    }

    scenario_index_entry Deserialise(IStream* stream) const override
    {
        scenario_index_entry item;

        stream->Read(item.path, sizeof(item.path));
        item.timestamp = stream->ReadValue<uint64_t>();

        item.category = stream->ReadValue<uint8_t>();
        item.source_game = stream->ReadValue<uint8_t>();
        item.source_index = stream->ReadValue<int16_t>();
        item.sc_id = stream->ReadValue<uint16_t>();

        item.objective_type = stream->ReadValue<uint8_t>();
        item.objective_arg_1 = stream->ReadValue<uint8_t>();
        item.objective_arg_2 = stream->ReadValue<int32_t>();
        item.objective_arg_3 = stream->ReadValue<int16_t>();
        item.highscore = nullptr;

        stream->Read(item.internal_name, sizeof(item.internal_name));
        stream->Read(item.name, sizeof(item.name));
        stream->Read(item.details, sizeof(item.details));

        return item;
    }

private:
    /**
     * Reads basic information from a scenario file.
     */
    static bool GetScenarioInfo(const std::string& path, uint64_t timestamp, scenario_index_entry* entry)
    {
        log_verbose("GetScenarioInfo(%s, %d, ...)", path.c_str(), timestamp);
        try
        {
            std::string extension = Path::GetExtension(path);
            if (String::Equals(extension, ".sc4", true))
            {
                // RCT1 scenario
                bool result = false;
                try
                {
                    auto s4Importer = ParkImporter::CreateS4();
                    s4Importer->LoadScenario(path.c_str(), true);
                    if (s4Importer->GetDetails(entry))
                    {
                        String::Set(entry->path, sizeof(entry->path), path.c_str());
                        entry->timestamp = timestamp;
                        result = true;
                    }
                }
                catch (const std::exception&)
                {
                }
                return result;
            }
            else
            {
                // RCT2 scenario
                auto fs = FileStream(path, FILE_MODE_OPEN);
                auto chunkReader = SawyerChunkReader(&fs);

                rct_s6_header header = chunkReader.ReadChunkAs<rct_s6_header>();
                if (header.type == S6_TYPE_SCENARIO)
                {
                    rct_s6_info info = chunkReader.ReadChunkAs<rct_s6_info>();
                    rct2_to_utf8_self(info.name, sizeof(info.name));
                    rct2_to_utf8_self(info.details, sizeof(info.details));
                    *entry = CreateNewScenarioEntry(path, timestamp, &info);
                    return true;
                }
                else
                {
                    log_verbose("%s is not a scenario", path.c_str());
                }
            }
        }
        catch (const std::exception&)
        {
            Console::Error::WriteLine("Unable to read scenario: '%s'", path.c_str());
        }
        return false;
    }

    static scenario_index_entry CreateNewScenarioEntry(const std::string& path, uint64_t timestamp, rct_s6_info* s6Info)
    {
        scenario_index_entry entry = {};

        // Set new entry
        String::Set(entry.path, sizeof(entry.path), path.c_str());
        entry.timestamp = timestamp;
        entry.category = s6Info->category;
        entry.objective_type = s6Info->objective_type;
        entry.objective_arg_1 = s6Info->objective_arg_1;
        entry.objective_arg_2 = s6Info->objective_arg_2;
        entry.objective_arg_3 = s6Info->objective_arg_3;
        entry.highscore = nullptr;
        if (String::IsNullOrEmpty(s6Info->name))
        {
            // If the scenario doesn't have a name, set it to the filename
            String::Set(entry.name, sizeof(entry.name), Path::GetFileNameWithoutExtension(entry.path));
        }
        else
        {
            String::Set(entry.name, sizeof(entry.name), s6Info->name);
            // Normalise the name to make the scenario as recognisable as possible.
            ScenarioSources::NormaliseName(entry.name, sizeof(entry.name), entry.name);
        }

        // entry.name will be translated later so keep the untranslated name here
        String::Set(entry.internal_name, sizeof(entry.internal_name), entry.name);

        String::Set(entry.details, sizeof(entry.details), s6Info->details);

        // Look up and store information regarding the origins of this scenario.
        source_desc desc;
        if (ScenarioSources::TryGetByName(entry.name, &desc))
        {
            entry.sc_id = desc.id;
            entry.source_index = desc.index;
            entry.source_game = desc.source;
            entry.category = desc.category;
        }
        else
        {
            entry.sc_id = SC_UNIDENTIFIED;
            entry.source_index = -1;
            if (entry.category == SCENARIO_CATEGORY_REAL)
            {
                entry.source_game = SCENARIO_SOURCE_REAL;
            }
            else
            {
                entry.source_game = SCENARIO_SOURCE_OTHER;
            }
        }

        scenario_translate(&entry, &s6Info->entry);
        return entry;
    }
};

class ScenarioRepository final : public IScenarioRepository
{
private:
    static constexpr char HEAD[] = "OpenRCT2";
    static constexpr char SCENS[] = "scen";
    static constexpr int32_t VERSION = 11;
    std::string _last_winner;

    std::shared_ptr<IPlatformEnvironment> const _env;
    ScenarioFileIndex const _fileIndex;
    std::vector<scenario_index_entry> _scenarios;
    std::vector<scenario_highscore_entry*> _highscores;

public:
    explicit ScenarioRepository(const std::shared_ptr<IPlatformEnvironment>& env)
        : _env(env)
        , _fileIndex(*env)
    {
    }

    virtual ~ScenarioRepository()
    {
        ClearHighscores();
    }

    void Scan(int32_t language) override
    {
        ImportMegaPark();

        // Reload scenarios from index
        _scenarios.clear();
        auto scenarios = _fileIndex.LoadOrBuild(language);
        for (auto scenario : scenarios)
        {
            AddScenario(scenario);
        }

        // Sort the scenarios and load the highscores
        Sort();
        LoadScores();
        LoadLegacyScores();
        AttachHighscores();
    }

    size_t GetCount() const override
    {
        return _scenarios.size();
    }

    const scenario_index_entry* GetByIndex(size_t index) const override
    {
        const scenario_index_entry* result = nullptr;
        if (index < _scenarios.size())
        {
            result = &_scenarios[index];
        }
        return result;
    }

    const scenario_index_entry* GetByFilename(const utf8* filename) const override
    {
        for (const auto& scenario : _scenarios)
        {
            const utf8* scenarioFilename = Path::GetFileName(scenario.path);

            // Note: this is always case insensitive search for cross platform consistency
            if (String::Equals(filename, scenarioFilename, true))
            {
                return &scenario;
            }
        }
        return nullptr;
    }

    const scenario_index_entry* GetByInternalName(const utf8* name) const override
    {
        for (size_t i = 0; i < _scenarios.size(); i++)
        {
            const scenario_index_entry* scenario = &_scenarios[i];

            if (scenario->source_game == SCENARIO_SOURCE_OTHER && scenario->sc_id == SC_UNIDENTIFIED)
                continue;

            // Note: this is always case insensitive search for cross platform consistency
            if (String::Equals(name, scenario->internal_name, true))
            {
                return &_scenarios[i];
            }
        }
        return nullptr;
    }

    const scenario_index_entry* GetByPath(const utf8* path) const override
    {
        for (const auto& scenario : _scenarios)
        {
            if (Path::Equals(path, scenario.path))
            {
                return &scenario;
            }
        }
        return nullptr;
    }

    bool TryRecordHighscore(int32_t language, const utf8* scenarioFileName, const utf8* scen_winner) override
    {
        // Scan the scenarios so we have a fresh list to query. This is to prevent the issue of scenario completions
        // not getting recorded, see #4951.
        Scan(language);
        money32 companyValue = gScenarioCompletedCompanyValue;
        int32_t dayRecord = gScenarioCompletedDays;

        scenario_index_entry* scenario = GetByFilename(scenarioFileName);
        if (scenario == nullptr)
            return false;

        // Check if record company value has been broken or record days to complete has been broken
        // Or, the company value and record is the tie but no name is registered
        scenario_highscore_entry* highscore = scenario->highscore;
        if ( (highscore != nullptr && companyValue <= highscore->company_value
            && highscore->record_days > 0 && dayRecord >= highscore->record_days)
            && ( (! String::IsNullOrEmpty(highscore->scen_winner) && String::IsNullOrEmpty(scen_winner))
                 || (companyValue < highscore->company_value && (dayRecord > highscore->record_days ))))
            return false;

        if (highscore == nullptr)
        {
            highscore = InsertHighscore();
            scenario->highscore = highscore;
        }
        else
        {
            SafeFree(highscore->scen_file);
            SafeFree(highscore->scen_winner);
        }
        // seconds from Jan 1, Year 2000 (UTC)
        highscore->timestamp = platform_get_datetime_now_utc() / 10000000 - 63017720400ULL;
        highscore->scen_file = String::Duplicate(Path::GetFileName(scenario->path));
        if (!String::IsNullOrEmpty(scen_winner))
            _last_winner = scen_winner;
        highscore->scen_winner = String::Duplicate(_last_winner);

        if (companyValue > highscore->company_value )
            highscore->company_value = companyValue;
        if (highscore->record_days == 0 || dayRecord < highscore->record_days)
            highscore->record_days = dayRecord;

        SaveHighscores();
        return true;
    }

private:
    scenario_index_entry* GetByFilename(const utf8* filename)
    {
        const ScenarioRepository* repo = this;
        return (scenario_index_entry*)repo->GetByFilename(filename);
    }

    scenario_index_entry* GetByPath(const utf8* path)
    {
        const ScenarioRepository* repo = this;
        return (scenario_index_entry*)repo->GetByPath(path);
    }

    /**
     * Mega Park from RollerCoaster Tycoon 1 is stored in an encrypted hidden file: mp.dat.
     * Decrypt the file and save it as sc21.sc4 in the user's scenario directory.
     */
    void ImportMegaPark()
    {
        auto mpdatPath = _env->GetFilePath(PATHID::MP_DAT);
        auto scenarioDirectory = _env->GetDirectoryPath(DIRBASE::USER, DIRID::SCENARIO);
        auto sc21Path = Path::Combine(scenarioDirectory, "sc21.sc4");
        if (File::Exists(mpdatPath) && !File::Exists(sc21Path))
        {
            ConvertMegaPark(mpdatPath, sc21Path);
        }
    }

    /**
     * Converts Mega Park to normalised file location (mp.dat to sc21.sc4)
     * @param srcPath Full path to mp.dat
     * @param dstPath Full path to sc21.dat
     */
    void ConvertMegaPark(const std::string& srcPath, const std::string& dstPath)
    {
        auto directory = Path::GetDirectory(dstPath);
        platform_ensure_directory_exists(directory.c_str());

        auto mpdat = File::ReadAllBytes(srcPath);

        // Rotate each byte of mp.dat left by 4 bits to convert
        for (size_t i = 0; i < mpdat.size(); i++)
        {
            mpdat[i] = rol8(mpdat[i], 4);
        }

        File::WriteAllBytes(dstPath, mpdat.data(), mpdat.size());
    }

    void AddScenario(const scenario_index_entry& entry)
    {
        auto filename = Path::GetFileName(entry.path);

        if (!String::Equals(filename, ""))
        {
            auto existingEntry = GetByFilename(filename);
            if (existingEntry != nullptr)
            {
                std::string conflictPath;
                if (existingEntry->timestamp > entry.timestamp)
                {
                    // Existing entry is more recent
                    conflictPath = String::ToStd(existingEntry->path);

                    // Overwrite existing entry with this one
                    *existingEntry = entry;
                }
                else
                {
                    // This entry is more recent
                    conflictPath = entry.path;
                }
                Console::WriteLine("Scenario conflict: '%s' ignored because it is newer.", conflictPath.c_str());
            }
            else
            {
                _scenarios.push_back(entry);
            }
        }
        else
        {
            log_error("Tried to add scenario with an empty filename!");
        }
    }

    void Sort()
    {
        if (gConfigGeneral.scenario_select_mode == SCENARIO_SELECT_MODE_ORIGIN)
        {
            std::sort(
                _scenarios.begin(), _scenarios.end(), [](const scenario_index_entry& a, const scenario_index_entry& b) -> bool {
                    return scenario_index_entry_CompareByIndex(a, b) < 0;
                });
        }
        else
        {
            std::sort(
                _scenarios.begin(), _scenarios.end(), [](const scenario_index_entry& a, const scenario_index_entry& b) -> bool {
                    return scenario_index_entry_CompareByCategory(a, b) < 0;
                });
        }
    }

    int32_t checksum(const scenario_highscore_entry* highscore)
    {
        return (highscore->timestamp ^ (highscore->timestamp << 8)
            + highscore->company_value + (highscore->company_value << 16)
            + (highscore->record_days << 8)) ^ 0x9E3779B9U; // golden ratio
    }

    void LoadScores()
    {
        std::string path = _env->GetFilePath(PATHID::SCORES);
        if (!File::Exists(path))
        {
            _last_winner = platform_get_username();
            return;
        }

        try
        {
            auto fs = FileStream( path, FILE_MODE_OPEN);
            auto reader = std::unique_ptr<IIniReader>(CreateIniReader(&fs));
            if (!reader->ReadSection(HEAD))
                throw IOException("hiscore1");
            int32_t version;
            version = reader->GetInt32("version", 0);

            _last_winner = reader->GetString("last_winner", _last_winner);
            int32_t cnt = reader->GetInt32("count", 0);
            char prefix[32], section[32];
            String::Set(prefix, sizeof prefix, reader->GetString("prefix", SCENS).c_str());

            ClearHighscores();

            for (int32_t i = 0; i < cnt; i++)
            {
                String::Format(section, sizeof section, "%s%d", prefix, i+1);
                if (!reader->ReadSection(section))
                    break;
                scenario_highscore_entry* highscore = InsertHighscore();
                highscore->timestamp = reader->GetInt32("timestamp", 0);
                highscore->scen_file = String::Duplicate((reader->GetString("file", "error")).c_str());
                highscore->scen_winner = String::Duplicate(reader->GetString("winner", "error").c_str());
                highscore->company_value = (money32)(reader->GetFloat("company_value", 0.0) * 10.0 + 0.5);
                highscore->record_days = reader->GetInt32("record_days", 0);
                if (checksum(highscore) != reader->GetInt32("checksum", 0))
                {
                    SafeFree(highscore->scen_winner);
                    highscore->company_value = 0; highscore->record_days = 0;
                    Console::Error::WriteLine("Highscore checksum mismatch");
                }
            }
        }
        catch (const std::exception&)
        {
            Console::Error::WriteLine("Error reading highscores.");
        }
    }

    void LoadScoresOld()
    {
        std::string path = _env->GetFilePath(PATHID::SCORES_OLD);
        if (!platform_file_exists(path.c_str()))
        {
            return;
        }

        try
        {
            auto fs = FileStream(path, FILE_MODE_OPEN);
            uint32_t fileVersion = fs.ReadValue<uint32_t>();
            if (fileVersion != 1)
            {
                Console::Error::WriteLine("Invalid or incompatible highscores file.");
                return;
            }

            ClearHighscores();

            uint32_t numHighscores = fs.ReadValue<uint32_t>();
            for (uint32_t i = 0; i < numHighscores; i++)
            {
                scenario_highscore_entry* highscore = InsertHighscore();
                highscore->scen_file = fs.ReadString();
                highscore->scen_winner = fs.ReadString();
                highscore->company_value = fs.ReadValue<money32>();
                highscore->timestamp = fs.ReadValue<datetime64>()/10000000;
            }
        }
        catch (const std::exception&)
        {
            Console::Error::WriteLine("Error reading old highscores.");
        }
    }

    /**
     * Loads the original scores.dat file and replaces any highscores that
     * are better for matching scenarios.
     */
    void LoadLegacyScores()
    {
        std::string rct2Path = _env->GetFilePath(PATHID::SCORES_RCT2);
        std::string legacyPath = _env->GetFilePath(PATHID::SCORES_LEGACY);
        LoadLegacyScores(legacyPath);
        LoadLegacyScores(rct2Path);
    }

    void LoadLegacyScores(const std::string& path)
    {
        if (!platform_file_exists(path.c_str()))
        {
            return;
        }

        bool highscoresDirty = false;
        try
        {
            auto fs = FileStream(path, FILE_MODE_OPEN);
            if (fs.GetLength() <= 4)
            {
                // Initial value of scores for RCT2, just ignore
                return;
            }

            // Load header
            auto header = fs.ReadValue<rct_scores_header>();
            for (uint32_t i = 0; i < header.ScenarioCount; i++)
            {
                // Read legacy entry
                auto scBasic = fs.ReadValue<rct_scores_entry>();

                // Ignore non-completed scenarios
                if (scBasic.Flags & SCENARIO_FLAGS_COMPLETED)
                {
                    bool notFound = true;
                    for (auto& highscore : _highscores)
                    {
                        if (String::Equals(scBasic.Path, highscore->scen_file, true))
                        {
                            notFound = false;

                            // Check if legacy highscore is better
                            if (scBasic.CompanyValue > highscore->company_value)
                            {
                                SafeFree(highscore->scen_winner);
                                std::string name = rct2_to_utf8(scBasic.CompletedBy, RCT2_LANGUAGE_ID_ENGLISH_UK);
                                highscore->scen_winner = String::Duplicate(name.c_str());
                                highscore->company_value = scBasic.CompanyValue;
                                highscore->timestamp = 0;
                                break;
                            }
                        }
                    }
                    if (notFound)
                    {
                        scenario_highscore_entry* highscore = InsertHighscore();
                        highscore->scen_file = String::Duplicate(scBasic.Path);
                        std::string name = rct2_to_utf8(scBasic.CompletedBy, RCT2_LANGUAGE_ID_ENGLISH_UK);
                        highscore->scen_winner = String::Duplicate(name.c_str());
                        highscore->company_value = scBasic.CompanyValue;
                        highscore->timestamp = 0;
                    }
                }
            }
        }
        catch (const std::exception&)
        {
            Console::Error::WriteLine("Error reading legacy scenario scores file: '%s'", path.c_str());
        }

        if (highscoresDirty)
        {
            SaveHighscores();
        }
    }

    void ClearHighscores()
    {
        for (auto highscore : _highscores)
        {
            scenario_highscore_free(highscore);
        }
        _highscores.clear();
    }

    scenario_highscore_entry* InsertHighscore()
    {
        auto highscore = new scenario_highscore_entry();
        memset(highscore, 0, sizeof(scenario_highscore_entry));
        _highscores.push_back(highscore);
        return highscore;
    }

    void AttachHighscores()
    {
        for (auto& highscore : _highscores)
        {
            scenario_index_entry* scenerio = GetByFilename(highscore->scen_file);
            if (scenerio != nullptr)
            {
                scenerio->highscore = highscore;
            }
        }
    }

    void SaveHighscores()
    {
        std::string path = _env->GetFilePath(PATHID::SCORES);
        try
        {
            auto fs = FileStream( path, FILE_MODE_WRITE);
            if (!fs.CanWrite())
                throw IOException("SaveHighscores");
            auto writer = std::unique_ptr<IIniWriter>(CreateIniWriter(&fs));

            writer->WriteSection(HEAD);
            writer->WriteInt32("version", VERSION);
            writer->WriteString("last_winner", _last_winner);
            writer->WriteString("prefix", SCENS);
            writer->WriteInt32("count", (int32_t)_highscores.size());

            char section[32];
            for (size_t i = 0; i < _highscores.size(); i++)
            {
                String::Format(section, sizeof section, "%s%d", SCENS, i + 1);
                writer->WriteSection(section);

                const scenario_highscore_entry* highscore = _highscores[i];
                writer->WriteInt32("timestamp", highscore->timestamp);
                writer->WriteFloat("company_value",
                    std::round( highscore->company_value *10.0)/100.0);
                writer->WriteInt32("record_days", highscore->record_days);
                writer->WriteString("file", highscore->scen_file);
                writer->WriteString("winner", highscore->scen_winner);
                writer->WriteInt32("checksum", checksum(highscore));

            }
        }
        catch (const std::exception&)
        {
            Console::Error::WriteLine("Unable to save highscores to '%s'", path.c_str());
        }
    }
};

std::unique_ptr<IScenarioRepository> CreateScenarioRepository(const std::shared_ptr<IPlatformEnvironment>& env)
{
    return std::make_unique<ScenarioRepository>(env);
}

IScenarioRepository* GetScenarioRepository()
{
    return GetContext()->GetScenarioRepository();
}

void scenario_repository_scan()
{
    IScenarioRepository* repo = GetScenarioRepository();
    repo->Scan(LocalisationService_GetCurrentLanguage());
}

size_t scenario_repository_get_count()
{
    IScenarioRepository* repo = GetScenarioRepository();
    return repo->GetCount();
}

const scenario_index_entry* scenario_repository_get_by_index(size_t index)
{
    IScenarioRepository* repo = GetScenarioRepository();
    return repo->GetByIndex(index);
}

bool scenario_repository_try_record_highscore(const utf8* scenarioFileName, const utf8* scen_winner)
{
    IScenarioRepository* repo = GetScenarioRepository();
    return repo->TryRecordHighscore(LocalisationService_GetCurrentLanguage(), scenarioFileName, scen_winner);
}

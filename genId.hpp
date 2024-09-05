#pragma once

#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

bool fileExists(const std::string &filename) {
    std::ifstream infile(filename);
    return infile.good();
}

bool sortedAndUnique(const std::vector<std::string> &keys) {
    for (int i = 1; i < keys.size(); ++i) {
        if (keys[i - 1] >= keys[i]) {
            return false;
        }
    }
    return true;
}

class IdGenerator {
  private:
    static const int ProvinceCodeCnt = 34;
    static const int CityCodeCnt = 80;
    static const int CountyCodeCnt = 70;
    static const int YearMin = 1949;
    static const int YearMax = 2024;
    static const int PoliceCodeCnt = 100;
    static const int GenderCodeCnt = 10;
    static const int CheckCodeCnt = 10;

    static const int ProvinceCodes[ProvinceCodeCnt];

    static int getProvinceCode() {
        return ProvinceCodes[rand() % ProvinceCodeCnt];
    }

    static int getCityCode() { return rand() % CityCodeCnt; }

    static int getCountyCode() { return rand() % CountyCodeCnt; }

    static int getYearCode() { return YearMin + rand() % (YearMax - YearMin); }

    static int getMonthDayCode() {
        int month = rand() % 12 + 1;
        switch (month) {
        case 4:
        case 6:
        case 9:
        case 11:
            return month * 100 + rand() % 30 + 1;
        case 2:
            return month * 100 + rand() % 28 + 1;
        default:
            return month * 100 + rand() % 31 + 1;
        }
    }

    static int getPoliceCode() { return rand() % PoliceCodeCnt; }

    static int getGenderCode() { return rand() % GenderCodeCnt; }

    static int getCheckCode() { return rand() % CheckCodeCnt; }

  public:
    static std::string getId() {
        std::string ret;
        ret += std::to_string(getProvinceCode());

        int CityCode = getCityCode();
        if (CityCode < 10) {
            ret += "0";
        }
        ret += std::to_string(CityCode);

        int CountyCode = getCountyCode();
        if (CountyCode < 10) {
            ret += "0";
        }
        ret += std::to_string(CountyCode);

        ret += std::to_string(getYearCode());

        int MonthDayCode = getMonthDayCode();
        if (MonthDayCode < 1000) {
            ret += "0";
        }
        ret += std::to_string(MonthDayCode);

        int PoliceCode = getPoliceCode();
        if (PoliceCode < 10) {
            ret += "0";
        }
        ret += std::to_string(PoliceCode);

        ret += std::to_string(getGenderCode());
        ret += std::to_string(getCheckCode());
        return ret;
    }

    static std::string getRandstr(int len = 20) {
        std::string ret;
        for (int i = 0; i < len; ++i) {
            ret.push_back(rand() % 26 + 'a');
        }
        return ret;
    }

    static std::vector<std::string> getKeys(int cnt, int type) {

        // The target file: Idcards.txt
        std::string filename = type == 0 ? "Idcards.txt" : "Randstr.txt";

        // Return Idcards
        std::vector<std::string> ids;

        if (fileExists(filename)) {
            std::cout << "Reading keys from " << filename << " ... "
                      << std::endl;
            std::ifstream infile(filename);
            std::string line;
            ids.clear();
            while (std::getline(infile, line)) {
                ids.push_back(line);
            }
            infile.close();
        } else {
            std::cout << "File not found. Generating keys ... " << std::endl;

            // Generate ids and store them in Idcards.txts
            std::set<std::string> idSet;
            while (idSet.size() < cnt) {
                std::string id = type == 0 ? getId() : getRandstr();
                idSet.insert(id);
            }
            ids.assign(idSet.begin(), idSet.end());
            std::sort(ids.begin(), ids.end()); // Sort the IDs
            std::cout << cnt << " Keys Generated in " << filename << std::endl;

            // Write the ids into Idcards.txt
            std::ofstream outFile(filename);
            if (!outFile.is_open()) {
                std::cerr << "Unable to open file: " << filename << std::endl;
                exit(0);
            }

            for (const auto &str : ids) {
                outFile << str << std::endl;
            }

            outFile.close();
        }

        return ids;
    }
};

const int IdGenerator::ProvinceCodes[IdGenerator::ProvinceCodeCnt] = {
    11, 12, 13, 14, 15, 21, 22, 23, 31, 32, 33, 34, 35, 36, 37, 41, 42,
    43, 44, 45, 46, 50, 51, 52, 53, 54, 61, 62, 63, 64, 65, 71, 81, 82};

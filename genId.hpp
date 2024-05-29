#pragma once

#include <algorithm>
#include <set>
#include <string>
#include <vector>

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

    static int getCityCode() {
        return rand() % CityCodeCnt;
    }

    static int getCountyCode() {
        return rand() % CountyCodeCnt;
    }

    static int getYearCode() {
        return YearMin + rand() % (YearMax - YearMin);
    }

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

    static int getPoliceCode() {
        return rand() % PoliceCodeCnt;
    }

    static int getGenderCode() {
        return rand() % GenderCodeCnt;
    }

    static int getCheckCode() {
        return rand() % CheckCodeCnt;
    }

public:
    static std::string getId() {
        std::string ret;
        ret += std::to_string(getProvinceCode());
        ret += std::to_string(getCityCode());
        ret += std::to_string(getCountyCode());
        ret += std::to_string(getYearCode());
        ret += std::to_string(getMonthDayCode());
        ret += std::to_string(getPoliceCode());
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

    static std::vector<std::string> getIds(int cnt) {
        std::set<std::string> idSet;  // Set to store unique IDs

        while (idSet.size() < cnt) {
            std::string id = getId();
            idSet.insert(id);
        }

        std::vector<std::string> ids(idSet.begin(), idSet.end());
        std::sort(ids.begin(), ids.end());  // Sort the IDs

        return ids;
    }

    static std::vector<std::string> getRandstrs(int cnt) {
        std::set<std::string> strSet;

        while (strSet.size() < cnt) {
            std::string id = getRandstr();
            strSet.insert(id);
        }

        std::vector<std::string> strs(strSet.begin(), strSet.end());
        std::sort(strs.begin(), strs.end());

        return strs;
    }
};

const int IdGenerator::ProvinceCodes[IdGenerator::ProvinceCodeCnt] = {
    11, 12, 13, 14, 15,
    21, 22, 23,
    31, 32, 33, 34, 35, 36, 37,
    41, 42, 43, 44, 45, 46,
    50, 51, 52, 53, 54,
    61, 62, 63, 64, 65,
    71,
    81, 82};

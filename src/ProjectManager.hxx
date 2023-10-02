#ifndef SRCLANG_PROJECTMANAGER_HXX
#define SRCLANG_PROJECTMANAGER_HXX

#include <filesystem>
#include <map>
#include <string>

namespace srclang {
    struct Language;

    class ProjectManager {
       private:
        Language *language{nullptr};
        std::map<std::string, std::string> config;
        std::filesystem::path projectPath;

        void load();

       public:
        explicit ProjectManager(Language *language, std::filesystem::path projectPath);

        void create(std::string const &projectName);

        void test();
    };

}  // srclang

#endif  // SRCLANG_PROJECTMANAGER_HXX

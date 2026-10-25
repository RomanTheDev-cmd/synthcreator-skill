#define main full_test_main
#include "/Users/roman/Library/Mobile Documents/com~apple~CloudDocs/Главная рабочая папка ИИ/Проекты/PowerKick/Tests/PowerKickTests.cpp"
#undef main
int main() { testMidiControllerChunking(); testCancelledHandoffStaysSmooth(); testWholeChainHandoff(); testRenderedKickStartsOnTheAttack(); std::cout << (failures ? "FAILED " : "PASS ") << checks - failures << "/" << checks << " checks\n"; return failures ? 1 : 0; }

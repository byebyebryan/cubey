#include <exception>
#include <iostream>
void test_headless_frame_submission_callbacks();
int main() {
    try {
        test_headless_frame_submission_callbacks();
        std::cout << "PASS: accepted/discarded host submission callbacks\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}

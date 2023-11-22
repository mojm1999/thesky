#include <iostream>
#include <memory>

void smartPointersExample()
{
    // 使用共享型智能指针
    std::shared_ptr<int> sptr1 = std::make_shared<int>(2);
    std::cout << *sptr1 << std::endl;
}

int main()
{
    std::cout << "Hello World." << std::endl;
    smartPointersExample();
    return 0;
}
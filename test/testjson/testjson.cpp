#include "json.hpp"
using json = nlohmann::json;

#include <iostream>
using namespace std;
#include <vector>
#include<map>

// json序列化示例
string func1()
{
    // 创建一个json对象
    json j;
    j["msg_type"] = 2;
    j["from"] = "zhangsan";
    j["to"] = "lisi";
    j["msg"] = "hello world";

    cout << j << endl;

    // 序列化 dump
    string str = j.dump();
    cout << "序列化后的json字符串为：" << str << endl; 
    return str;
}
void func2()
{
    // 创建一个json对象
    json j;
    j["id"] = {1,2,3,4,5};
    j["msg"]["zhangsan"] = "hello";
    j["msg"]["lisi"] = "world";
    // 等价于 j["msg"] = {{"zhangsan", "hello"}, {"lisi", "world"}};
    cout << "json对象为：" << j << endl;

    string str = j.dump();
    cout << "序列化后的json字符串为：" << str << endl; 
}
// 容器序列化
void func3()
{
    // 创建一个json对象
    json j;
    vector<int> vec = {1,2,3,4,5};
    map<int, string> m;
    m.insert({1, "hello"});
    m.insert({2, "world"});


    j["vec"] = vec;
    j["map"] = m;

    cout << "json对象为：" << j << endl;

    string str = j.dump();
    cout << "序列化后的json字符串为：" << str << endl; 
}


int main()
{
    // func1();
    // func2();
    //func3();

    string recstr = func1();
    // 反序列化 parse
    json j2 = json::parse(recstr);
    cout << "反序列化后的json对象为：" << j2 << endl;
    cout << "msg_type: " << j2["msg_type"] << endl;




    return 0;
}
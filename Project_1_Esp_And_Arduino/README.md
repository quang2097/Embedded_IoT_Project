Quy luật đặt tên: tất cả đều phải bằng tiếng Anh, mỗi folder hoặc file phải có toàn bộ là chữ in hoa chữ cái đầu đi kèm với dấu "_" phân cách mỗi từ.
VD: "Example_name.txt"

Folder dự án phải được sắp xếp như sau:

    Root
        [Project_Name_1]
            [Device_Side]
            [Server_Side] (nếu làm hệ thống IoT)
        .gitignore
        LICENSE
        README.md

Quy luật format code:

    #PYTHON
    Thành phần quy tắc Google
    Indentation                     4 spaces, không dùng tab
    Độ dài dòng	Tối đa              80 ký tự
    Tên biến/hàm	                snake_case
    Tên class	                    PascalCase
    Hằng số	                        ALL_CAPS
    Tên module	                    snake_case.py
    Import	                        Nhóm theo standard library → third-party → code của project
    Semicolon ;	                    Không dùng
    Docstring	                    Dùng """triple double quotes"""
    Type annotation	                Khuyến khích, đặc biệt với API công khai
    Main program	                Dùng main() + if __name__ == "__main__":
    Global mutable state	        Tránh
    Default argument	            Không dùng [] hoặc {} làm default
    Boolean	                        Thường dùng if not x: thay vì if x == False

    #CPP
    Thành phần quy tắc Google
    Indentation                     2 spaces, không dùng tab
    Độ dài dòng	Tối đa                              80 ký tự
    Function / variable	PascalCase cho functions?   Không — Google dùng PascalCase cho functions và variables thường snake_case
                                                    Class / struct	PascalCase
    Namespace	                    snake_case
    Constants	                    kPascalCase
    File C++	                    .cpp
    Header	                        .h
    Pointer/reference	            int* ptr, const std::string& name
    Braces	                        { nằm cùng dòng
    Tabs	                        Không dùng
    Macros	                        Tránh sử dụng
    Header guard	                Dùng include guards, không dùng #pragma once theo guide
    C++ version	                    C++20 hiện tại
    Blank lines	                    Hạn chế, dùng có mục đích
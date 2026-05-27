// SQLBindCol_ref.cpp  
// compile with: odbc32.lib  
#include <windows.h>  
#include <stdio.h>  
#include <locale.h> // 한글 출력을 위한 로케일 헤더 추가

#define UNICODE  
#include <sqlext.h>  

#define NAME_LEN 20  

void show_error()
{
    printf("SQL Execution Error 혹은 추가 정보가 있습니다.\n");
}

int main()
{
    // 콘솔 창에서 한글(유니코드) 출력이 깨지지 않도록 로케일 설정
    setlocale(LC_ALL, "");

    SQLHENV henv;
    SQLHDBC hdbc;
    SQLHSTMT hstmt = 0;
    SQLRETURN retcode;

    SQLINTEGER user_id = 0, user_level = 0;
    SQLWCHAR user_name[NAME_LEN] = { 0 };
    SQLLEN cb_name = 0, cb_id = 0, cb_level = 0;

    // 1. Environment handle 할당
    retcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);

    if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
    {
        // 2. ODBC 버전 환경 속성 설정
        retcode = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);

        if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
        {
            // 3. Connection handle 할당
            retcode = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);

            if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
            {
                // 로그인 타임아웃 5초 설정
                SQLSetConnectAttr(hdbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);

                // 4. 데이터 소스 연결 (성공하신 ODBC 이름: 2026GameServer)
                retcode = SQLConnect(hdbc, (SQLWCHAR*)L"2026GameServer", SQL_NTS, (SQLWCHAR*)NULL, 0, NULL, 0);

                if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
                {
                    // 5. Statement handle 할당
                    retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);

                    // 6. SQL 쿼리 실행 (오타 수정: user_name)
                    retcode = SQLExecDirect(hstmt, (SQLWCHAR*)L"SELECT user_id, user_name, user_level FROM user_table ORDER BY 1, 2, 3", SQL_NTS);

                    if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
                    {
                        // 7. 컬럼 바인딩 (수정: 바이트 크기 단위 단위 적용 및 크기 수정)
                        retcode = SQLBindCol(hstmt, 1, SQL_C_LONG, &user_id, sizeof(user_id), &cb_id);
                        retcode = SQLBindCol(hstmt, 2, SQL_C_WCHAR, user_name, sizeof(user_name), &cb_name);
                        retcode = SQLBindCol(hstmt, 3, SQL_C_LONG, &user_level, sizeof(user_level), &cb_level);

                        // 8. 데이터 페치 및 출력
                        for (int i = 0; ; i++)
                        {
                            retcode = SQLFetch(hstmt);

                            if (retcode == SQL_ERROR)
                            {
                                show_error();
                                break;
                            }

                            if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
                            {
                                // 서식 지정자 수정: %d (정수), %ls (유니코드 문자열), %d (정수)
                                printf("%d: ID=%d, Name=%ls, Level=%d\n", i + 1, user_id, user_name, user_level);
                            }
                            else if (retcode == SQL_NO_DATA)
                            {
                                // 데이터를 모두 읽었으면 루프 종료
                                break;
                            }
                        }
                    }
                    else
                    {
                        printf("쿼리 실행 실패!\n");
                    }

                    // 자원 해제
                    if (hstmt)
                    {
                        SQLCancel(hstmt);
                        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
                    }
                }
                else
                {
                    printf("DB 연결 실패!\n");
                }

                SQLDisconnect(hdbc);
            }
            SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
        }
        SQLFreeHandle(SQL_HANDLE_ENV, henv);
    }
}
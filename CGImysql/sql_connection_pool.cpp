#include <pthread.h>
#include "sql_connection_pool.h"

using namespace std;

connection_pool::connection_pool()
{
	this->CurConn = 0;
	this->FreeConn = 0;
}


connection_pool *connection_pool::GetInstance()
{
	static connection_pool connPool;
	return &connPool;
}

//构造初始化
// void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, unsigned int MaxConn)
// {
// 	this->url = url;
// 	this->Port = Port;
// 	this->User = User;
// 	this->PassWord = PassWord;
// 	this->DatabaseName = DBName;

// 	lock.lock();
// 	for (int i = 0; i < MaxConn; i++)
// 	{
// 		MYSQL *con = NULL;
// 		con = mysql_init(con);
// 		if (con == NULL)
// 		{
// 			cout << "Error:" << mysql_error(con)<<endl;
// 			lock.unlock();
// 			exit(1);
// 		}
// 		con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);

// 		if (con == NULL)
// 		{
// 			cout << "Error: " << mysql_error(con)<<endl;
// 			lock.unlock();
// 			exit(1);
// 		}
// 		connList.push_back(con);
// 		++FreeConn;
// 	}

// 	reserve = sem(FreeConn);

// 	this->MaxConn = FreeConn;
	
// 	lock.unlock();
// }

//初始化函数 -2
void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, unsigned int MaxConn)
{
	//关于该函数的补充说明：
	/*
	原函数是用的MySQL指针，使用MySQL_init函数在堆上动态创建的空间，最后使用MySQL_close函数动态释放空间。、
	本函数是使用MySQL对象，是在栈上创建的空间，函数退出则对象被清理，所以运行会有段错误。可以改为static MySQL con;
	*/
	this->url = url;
	this->Port = Port;
	this->User = User;
	this->PassWord = PassWord;
	this->DatabaseName = DBName;
	lock.lock();
	for (int i = 0; i < MaxConn; i++)
	{
		static MYSQL con;
		mysql_init(&con);
		if (!mysql_real_connect(&con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0)){
			//printf("错误原因：%s\n", mysql_error(&con));
			cout << "Error: MySQL连接失败，" << mysql_error(&con)<<endl;
			lock.unlock();
			exit(1);
		}
		connList.push_back(&con);
		++FreeConn;
	}
	reserve = sem(FreeConn);
	this->MaxConn = FreeConn;
	lock.unlock();
}

//当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection()
{
	MYSQL *con = NULL;

	if (0 == connList.size())
		return NULL;

	reserve.wait();
	
	lock.lock();

	con = connList.front();
	connList.pop_front();

	--FreeConn;
	++CurConn;

	lock.unlock();
	return con;
}

//释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL *con)
{
	if (NULL == con)
		return false;

	lock.lock();

	connList.push_back(con);
	++FreeConn;
	--CurConn;

	lock.unlock();

	reserve.post();
	return true;
}

//销毁数据库连接池
void connection_pool::DestroyPool()
{

	lock.lock();
	if (connList.size() > 0)
	{
		list<MYSQL *>::iterator it;
		for (it = connList.begin(); it != connList.end(); ++it)
		{
			MYSQL *con = *it;
			mysql_close(con);
		}
		CurConn = 0;
		FreeConn = 0;
		connList.clear();

		lock.unlock();
	}

	lock.unlock();
}

//当前空闲的连接数
int connection_pool::GetFreeConn()
{
	return this->FreeConn;
}

connection_pool::~connection_pool()
{
	DestroyPool();
}

connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool)
{
	*SQL = connPool->GetConnection();
	conRAII = *SQL;
	poolRAII = connPool;
}

connectionRAII::~connectionRAII()
{
	poolRAII->ReleaseConnection(conRAII);
}
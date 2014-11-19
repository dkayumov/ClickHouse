#include <DB/IO/Operators.h>
#include <DB/IO/WriteBufferFromFileDescriptor.h>


int main(int argc, char ** argv)
{
	DB::WriteBufferFromFileDescriptor buf(STDOUT_FILENO);
	buf
		<< "Hello, world!" << '\n'
		<< DB::escape << "Hello, world!" << '\n'
		<< DB::quote << "Hello, world!" << '\n'
		<< DB::double_quote << "Hello, world!" << '\n'
		<< DB::binary << "Hello, world!" << '\n'
		<< mysqlxx::DateTime(time(0)) << '\n'
		<< mysqlxx::Date(time(0)) << '\n'
		<< 1234567890123456789UL << '\n'
		<< DB::flush;

	return 0;
}
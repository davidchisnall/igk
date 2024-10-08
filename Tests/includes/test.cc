
// Fake ostream so that we don't need to include iostream
namespace std {
	struct ostream {
		ostream& operator<<(const char*);
		ostream& operator<<(int);
		ostream& operator<<(std::ostream& (*)(std::ostream&));
	};
	ostream cout;
	int endl;
}

/// This is a macro
#define MACRO 1

/**
 * This is a macro with an argument
 */
#define MACRO_ARG(x, y, z) (x + 1 + y / z)

// example1#begin
static int y;

class Foo {};

namespace NamespaceX {
  class Bar {};
}

__attribute__((cheri_libcall))
void libcall();

/**
 * This is the `main` function!  Yes it is!
 *
 * MAAAAAIIIIN!
 *
 *  - Is it?
 *  - Yes, it is!
 *  - It takes `argc` and `argv` as arguments.
 */
int main(int argc, char **argv) {
	// Comment
  std::cout << "Hello, World!" << std::endl;
  return 0;
}
// example1#end

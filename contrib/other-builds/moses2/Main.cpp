#include <iostream>
#include "StaticData.h"
#include "Manager.h"
#include "Phrase.h"
#include "moses/InputFileStream.h"

using namespace std;

int main()
{
	cerr << "Starting..." << endl;

	StaticData staticData;

	string line;
	while (getline(cin, line)) {
		Phrase *input = Phrase::CreateFromString(NULL, line);

		Manager mgr(staticData, *input);

		delete input;
	}

	cerr << "Finished" << endl;
}

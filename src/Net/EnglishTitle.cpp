/*
 * EnglishTitle.cpp
 *
 *  Created on: 9 Jul 2015
 *      Author: neil
 */

#include <Utility/fileutils.h>
#include <Utility/Output.h>
#include <IOC/Runnable.h>
#include <IOC/BuilderNParams.h>

namespace Net {

class EnglishTitle : public IOC::Runnable
{
private:
	std::vector< std::string > m_wordList;
	std::vector< std::string > m_inputLines;
	Utility::OutputPtr m_output;
public:

	EnglishTitle( std::vector< std::string > const& inputLanguageFiles,
			std::vector< std::string > const& inputLines,
			Utility::OutputPtr output )
	: m_inputLines( inputLines )
	, m_output( output )
	{
		for( auto const& langFile : inputLanguageFiles )
		{
			Utility::fileBasedCollection( langFile, &m_wordList, Utility::capitaliseConverter() );
		}
		std::sort( m_wordList.begin(), m_wordList.end() );
	}

protected:
	int doRun()
	{
		for( auto const& inputLine: m_inputLines )
		{
			std::istringstream iss( inputLine );
			std::string word;
			size_t wordCount = 0;
			size_t found = 0;
			while( iss >> word )
			{
				std::string use( Utility::capitalise( Utility::trim2( word ) ) );
				if( use.size() > 2 && use[0] >= 'A' )
				{
					++wordCount;
					auto iter( std::lower_bound( m_wordList.begin(), m_wordList.end(), use ));
					if( iter != m_wordList.end() && *iter == use )
					{
						++found;
					}
				}
			}
			int score = -1;
			if( wordCount > 0)
			{
				score = (found * 100) / wordCount;
			}
			m_output->os() << inputLine << '\t' << score << '\t' << wordCount << '\t' << found << "\r\n";
		}
		m_output->flush();
		return 0;
	}
};

typedef IOC::Builder3Params< EnglishTitle, IOC::Runnable, std::vector<std::string>, std::vector<std::string>, Utility::Output>
	EnglishTitleBuilder;

}

extern "C" {
	IOC_API IOC::BuilderFactoryImpl< Net::EnglishTitleBuilder > g_EnglishTitle;
}

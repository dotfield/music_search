/*
 * iTunes.cpp
 *
 *  Created on: 26 Feb 2014
 *      Author: neil
 */

// function to go to iTunes and look up a request

#include <Poco/Net/HTTPClientSession.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/StreamCopier.h>
//#include <Poco/JSON/JSON.h>
#include <Poco/JSON/Parser.h>

#include <Poco/Path.h>
#include <Poco/URI.h>
#include <Poco/Exception.h>

#include <iostream>
#include <string>
#include <cctype>
#include <Utility/strutils.h>
#include <Utility/fileutils.h>
#include <Utility/Output.h>
#include <Utility/input.h>
#include <IOC/Runnable.h>
#include <IOC/BuilderNParams.h>
#include <boost/thread.hpp>

class ITunesSearch
{
	Poco::URI m_uri;
	std::set< std::string > m_filteredGenres; // genres we exclude
	Utility::OutputPtr m_output;

public:

	ITunesSearch( Utility::OutputPtr output, std::set< std::string > const& filteredGenres )
		: m_uri( "http://itunes.apple.com" ),
		  m_filteredGenres(filteredGenres),
		  m_output( output )
	{
	}

	std::string make_request( std::string const& term ) const
	{
		std::string res = m_uri.getPathAndQuery();
		res.append( "/search?term=" );
		res.reserve( res.size() + term.size() + 11 );

		for( auto ch : term )
		{
			if( ch < 32 || ch > 127 )
				continue;

			if( isspace(ch) )
			{
				res.push_back('+');
			}
			else if( isalnum( ch ))
			{
				res.push_back( ::tolower( ch ) );
			}
			else
			{
				char ascii[8];
				sprintf( ascii, "%%%2x", ch ); // convert characters like &
				res.append( ascii );
			}
		}
		res.append( "&country=gb" );
		return res;
	}

	void doSearchMT( std::istream& input, size_t numThreads , size_t startLine)
	{
		Utility::MTInput mtInput( input );
		Utility::MTOutput mtOutput( m_output );
		Utility::OutputPtr consoleOutput( new Utility::ConsoleOutput );
		Utility::MTOutput mtConsoleOutput( consoleOutput );

		if( numThreads < 2 )
		{
			doSearch( mtInput, mtOutput, mtConsoleOutput, startLine );
		}
		else
		{
			boost::thread_group group;
			for( size_t i = 0; i < numThreads; ++i )
			{
				group.create_thread
				  ( [this, &mtInput, &mtOutput, &mtConsoleOutput, startLine]
				     {
						doSearch( mtInput, mtOutput, mtConsoleOutput, startLine );
				     }
				  );
			}
			group.join_all();
		}
	}

	void doSearch( Utility::MTInput & input,
			Utility::MTOutput & mtOutput,
			Utility::MTOutput & mtConsoleOutput,
			size_t startLine ) const
	{
		Poco::Net::HTTPClientSession session( m_uri.getHost(), m_uri.getPort() );
		{
			Utility::MTOutput::unique_lock lock( mtConsoleOutput.acquire());
			mtConsoleOutput.os() << "Thread " << pthread_self() << " starting ";
			mtConsoleOutput.flush();
		}

		std::string line;

		while( size_t lineNum = input.getline( line ))
		{
			if( lineNum < startLine )
				continue;

			std::string path;
			bool success = false;
			size_t maxTries = 4;
			while( !success && maxTries )
			{
				try
				{
					Utility::StrTuple term = Utility::delimitedText( line, '\t' );
					if( term.size() < 4 )
						break;

					std::string artist = term[2];
					std::string title = Utility::trim(term[3]);
					path = make_request( artist + ' ' + title );
					Poco::Net::HTTPRequest request( Poco::Net::HTTPRequest::HTTP_GET, path, Poco::Net::HTTPMessage::HTTP_1_1 );
					Poco::Net::HTTPResponse response;
					session.sendRequest(request);
					std::istream& rs = session.receiveResponse(response);

					using Poco::Dynamic::Var;
					using namespace Poco::JSON;
					Parser parser;
					Var result = parser.parse(rs);
					Object::Ptr obj = result.extract<Object::Ptr>();
					int nResults = obj->getValue<int>("resultCount");

					{
						Utility::MTOutput::unique_lock lock( mtConsoleOutput.acquire());
						mtConsoleOutput.os() << lineNum << ": " << path << ' ' << nResults
								<< " results " << std::endl;
					}

					if( nResults )
					{
						Array::Ptr arr;
						arr = obj->getArray( "results" );
						if( !arr )
						{
							std::cerr << "Could not get results " << std::endl;
							continue;
						}
						std::string releaseDate;
						std::string genre;
						std::string artistName; // on iTunes
						std::string trackName; // on iTunes
						bool found = false;
						// if there is more than one see if there is an exact match. Otherwise use the first result
						for( size_t i = 0; !found && i < nResults ; ++i )
						{
							Object::Ptr result = arr->getObject(i);
							if( !result )
							{
								std::cerr << " Could not get result " << i << std::endl;
								continue;
							}
							// get ArtistName and Title and see if they match ours
							Var item = result->get( "artistName" );
							if( item.isString() )
								artistName = item.convert< std::string >();

							item = result->get( "trackName" );

							if( item.isString() )
								trackName = item.convert< std::string >();

							if( (artistName == artist && trackName == title) ) // we have an exact match so continue
								found = true;

							// if no exact matches are found we use the first one.
							// We could use a better way to match the search eg case insensitive, removing featured acts etc.
							if( found || i == 0 )
							{
								item = result->get( "releaseDate");
								if( item.isString() )
								{
									std::string releaseDateStr = item.convert< std::string >();
									releaseDate = releaseDateStr.substr( 0, releaseDateStr.find('T') );
								}
								item = result->get( "primaryGenreName" );
								if( item.isString() )
									genre = item.convert< std::string >();
							}
						}

						if( m_filteredGenres.count( genre ) == 0 )
						{
							Utility::MTOutput::unique_lock lock( mtOutput.acquire());
						// output the result. Spotify link, artist(spotify), title(spotify)
						// artist(iTunes), title(iTunes), releaseDate, genre, numTracks (term[4])
							mtOutput.os() << lineNum << '\t' <<
									term[0] << '\t' << term[1] << '\t' << artist << '\t' << title << '\t' <<
									artistName << '\t' << trackName << '\t' << releaseDate << '\t' << genre << '\t' <<  term[4] << '\n';

							mtOutput.flush();
						}
					}
					success = true;
				}
				catch( std::exception const& err )
				{
					success = false;
					if( --maxTries )
					{
						sleep(1);
					}
					else
					{
						Utility::MTOutput::unique_lock lock( mtConsoleOutput.acquire());
						std::cerr << "ERROR: " << err.what() << lineNum << ": " << path  << std::endl;
					}
				}
			}
		}
		{
			Utility::MTOutput::unique_lock lock( mtConsoleOutput.acquire());
			mtConsoleOutput.os() << "Thread " << pthread_self() << " exiting\n";
			mtConsoleOutput.flush();
		}
	}
};

class ITunesSearchRun : public IOC::Runnable
{
private:
	std::ifstream m_inputFile;
	Utility::OutputPtr m_output;
	std::set< std::string > m_filteredGenres;
	size_t m_numThreads;
	size_t m_startLine;

public:

	ITunesSearchRun( std::string const& input,
			Utility::OutputPtr output,
			std::set< std::string > filteredGenres,
			size_t numThreads,
			size_t startLine ) :
		m_inputFile( input ),
		m_output( output ),
		m_filteredGenres( filteredGenres ),
		m_numThreads( numThreads ),
		m_startLine( startLine )
	{
		if( !m_inputFile.is_open() )
		{
			std::ostringstream oss;
			oss << "Error opening input file " << oss.str();
		}
	}

protected:

	int doRun()
	{
		ITunesSearch search( m_output, m_filteredGenres );
		search.doSearchMT( m_inputFile, m_numThreads, m_startLine );

		return 0;
	}
};

using namespace IOC;

typedef Builder5Params< ITunesSearchRun, Runnable, std::string, Utility::Output, std::set< std::string >, size_t, size_t > ITunesSearchRunBuilder;
extern "C" {
	IOC_API BuilderFactoryImpl< ITunesSearchRunBuilder > g_ITunesSearchRun;
}



# music_search
Spotify and iTunes search programs

This requires also the ioc code from the ioc repository
It also uses Poco libraries which you will need to install

The Spotify search originally used the old SDK and was rewritten
to use the new Spotify API however it could have been written in
Python with very few dependencies. I may rewrite it in Python
someday.

The iTunes search could also have been written in Python as that
also does HTTP Get and JSON parsing of the results. Slightly less
complex in that it does not do further search requests.



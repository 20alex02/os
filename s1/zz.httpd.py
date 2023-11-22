from aiohttp import web
import asyncio

sock = 'zt.test_httpd.sock'
app = web.Application()

async def hello( request ):
    return web.Response( text = "hello\n" )
async def bye( request ):
    return web.Response( text = "bye\n" )

app.add_routes( [ web.get( '/hello.txt', hello ),
                  web.get( '/bye.txt', bye ) ] )

async def main():
    runner = web.AppRunner( app )
    await runner.setup()
    site = web.UnixSite( runner, sock )
    await site.start()

    print( 'listening for connections on', sock )

    while True:
        await asyncio.sleep( 60 )

asyncio.run( main() )

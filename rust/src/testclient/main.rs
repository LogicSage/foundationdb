use foundationdb::endpoints::{network_test, ping_request};
use foundationdb::flow::{uid::WLTOKEN, Result};
use foundationdb::services::{ConnectionKeeper, LoopbackHandler};
use std::net::{IpAddr, Ipv4Addr, SocketAddr};

#[tokio::main]
async fn main() -> Result<()> {
    let loopback_handler = LoopbackHandler::new()?;
    loopback_handler
        .register_well_known_endpoint(WLTOKEN::PingPacket, Box::new(ping_request::Ping::new()));
    loopback_handler.register_well_known_endpoint(
        WLTOKEN::ReservedForTesting,
        Box::new(network_test::NetworkTest::new()),
    );
    let pool = ConnectionKeeper::new(None, loopback_handler);

    let saddr = SocketAddr::new(IpAddr::V4(Ipv4Addr::new(127, 0, 0, 1)), 6789);
    ping_request::ping(saddr, &pool).await?;
    println!("got ping response from {:?}", saddr);
    network_test::network_test(saddr, &pool, 100, 100).await?;

    println!("Goodbye, cruel world!");

    Ok(())
}
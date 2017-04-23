using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Reactive;
using System.Reactive.Concurrency;
using System.Reactive.Linq;
using System.Reactive.Threading.Tasks;
using Nito.AsyncEx;
using RJCP.IO.Ports;
namespace IoT_Controller
{
    public static class SerialPortStreamExt
    {
        public static async Task SendLightQueryPacketAsync(this SerialPortStream stream)
        {
            byte[] buffer = new byte[3];
            buffer[0] = 0;
            buffer[1] = 0;
            buffer[2] = unchecked((byte)-1);
            string encoded_packet = Convert.ToBase64String(buffer);
            encoded_packet += '\n';
            buffer = Encoding.ASCII.GetBytes(encoded_packet);
            //Console.WriteLine(encoded_packet);
            await stream.WriteAsync(buffer, 0, buffer.Length);
            
        }
        public static IObservable<byte[]> ObserveBytes(this SerialPortStream stream)
        {
            byte[] buffer = new byte[4096];
            return Observable.Create<byte[]>(async obs => {
                while (stream.IsOpen) {
                    int rx = await stream.ReadAsync(buffer, 0, 4096).ConfigureAwait(false);
                    //Console.WriteLine($"Got {rx} bytes");
                    if (rx != 0) {
                        obs.OnNext(buffer.Take(rx).ToArray());
                    }
                }
            });
        }
    }
    class Program
    {
        static byte[] DecodePacket(IList<byte> bytes)
        {
            try {
                byte[] packet = Convert.FromBase64String(Encoding.ASCII.GetString(bytes.ToArray()));
                return packet;
            } catch (Exception ex) {
                return null;
            }
        }
        static async Task MainAsync(string[] args)
        {
            using (SerialPortStream stream = new SerialPortStream("COM5")) {
                stream.BaudRate = 115200;
                stream.Open();

                var byteSource = stream.ObserveBytes()
                    .SelectMany(bytes => bytes)

                    .ObserveOn(NewThreadScheduler.Default)
                    .Publish();

                var newlineSource = byteSource
                    .Where(value => value == '\n');

                var packetSource = byteSource
                    .Buffer(newlineSource)
                    .Select(bytes => DecodePacket(bytes))
                    .Where(packet => packet != null);

                var intSub = packetSource
                    .Where(packet => packet[0] == 1)
                    .ObserveOn(NewThreadScheduler.Default)
                    .Subscribe(packet => {
                        Console.WriteLine(BitConverter.ToInt16(packet, 3));
                        stream.SendLightQueryPacketAsync().ToObservable().Wait();
                        });

                var stringSub = packetSource
                    .Where(packet => packet[0] == 0)
                    .ObserveOn(NewThreadScheduler.Default)
                    .Subscribe(packet => Console.WriteLine(Encoding.ASCII.GetString(packet, 3, packet[1])));
                
                await stream.SendLightQueryPacketAsync();
                using (byteSource.Connect()) {
                    await Observable.Never<object>();
                }
                

            }
        }


        static void Main(string[] args)
        {
            AsyncContext.Run(() => MainAsync(args));
           
        }
    }
}

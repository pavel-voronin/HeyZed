import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.security.KeyPairGenerator;
import java.security.MessageDigest;
import java.util.HexFormat;
import java.util.List;
import java.util.Map;

import me.zed_0xff.zombie_buddy.KnownAuthors;
import me.zed_0xff.zombie_buddy.SteamWorkshop.SteamID64;
import me.zed_0xff.zombie_buddy.ZBSVerifier;
import zb.org.bouncycastle.crypto.params.Ed25519PrivateKeyParameters;
import zb.org.bouncycastle.crypto.signers.Ed25519Signer;
import zb.org.bouncycastle.crypto.util.PrivateKeyFactory;

                                                                                       
public final class ZbsSign {
    private static final HexFormat HEX = HexFormat.of();

    public static void main(String[] args) throws Exception {
        if (args.length == 2 && args[0].equals("init")) {
                                                                                                
            var pair = KeyPairGenerator.getInstance("Ed25519").generateKeyPair();
            Files.write(Path.of(args[1]), pair.getPrivate().getEncoded(), StandardOpenOption.CREATE_NEW);
            printPublicKey(readKey(Path.of(args[1])));
            return;
        }
        if (args.length != 5 || !(args[0].equals("sign") || args[0].equals("verify-steam"))) {
            throw new IllegalArgumentException("init <private.der> | sign/verify-steam <private.der> <SteamID64> <jar> <signature>");
        }
        if (!args[2].matches("[0-9]{17}")) throw new IllegalArgumentException("Expected a 17-digit SteamID64.");
        var sid = new SteamID64(args[2]);
        Path jar = Path.of(args[3]), signature = Path.of(args[4]);
        String hash = HEX.formatHex(MessageDigest.getInstance("SHA-256").digest(Files.readAllBytes(jar)));
        if (args[0].equals("verify-steam")) {
            requireValid(ZBSVerifier.verify(jar, signature, hash, sid));
            System.out.println("PASS: installed ZombieBuddy verified the JAR against the public Steam profile.");
            return;
        }
        var key = readKey(Path.of(args[1]));
        String publicHex = HEX.formatHex(key.generatePublicKey().getEncoded());
        byte[] payload = ("ZBS:" + sid + ":" + hash).getBytes(StandardCharsets.UTF_8);
        var signer = new Ed25519Signer();
        signer.init(true, key);
        signer.update(payload, 0, payload.length);
        Files.writeString(signature, "ZBS\nSteamID64:" + sid + "\nSignature:"
            + HEX.formatHex(signer.generateSignature()) + "\n", StandardCharsets.UTF_8,
            StandardOpenOption.CREATE_NEW);

                                                                                             
        var author = new KnownAuthors.AuthorEntry();
        author.id = sid;
        author.keys = List.of(publicHex);
        var localKey = Map.of(sid, author);
        requireValid(ZBSVerifier.verify(jar, signature, hash, sid, localKey));
        String alteredHash = (hash.charAt(0) == '0' ? "1" : "0") + hash.substring(1);
        if (!(ZBSVerifier.verify(jar, signature, alteredHash, sid, localKey) instanceof ZBSVerifier.InvalidSignature)) {
            throw new IllegalStateException("Verifier accepted a changed JAR hash.");
        }
        if (!(ZBSVerifier.verify(jar, signature, hash, new SteamID64(sid.value() + 1), localKey)
                instanceof ZBSVerifier.InvalidSignature)) {
            throw new IllegalStateException("Verifier accepted a different Workshop author.");
        }
        System.out.println("PASS: signature verified locally; changed JAR and wrong author rejected.");
        printPublicKey(key);
    }

    private static Ed25519PrivateKeyParameters readKey(Path file) throws Exception {
        return (Ed25519PrivateKeyParameters) PrivateKeyFactory.createKey(Files.readAllBytes(file));
    }

    private static void printPublicKey(Ed25519PrivateKeyParameters key) {
        System.out.println("JavaModZBS:" + HEX.formatHex(key.generatePublicKey().getEncoded()));
    }

    private static void requireValid(ZBSVerifier.Verification result) {
        if (!(result instanceof ZBSVerifier.ValidSignature)) {
            throw new IllegalStateException(result.detailedMessage);
        }
    }
}
